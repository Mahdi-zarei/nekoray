package main

import (
	"ThroneCore/gen"
	"ThroneCore/internal/boxbox"
	"ThroneCore/internal/boxdns"
	"ThroneCore/internal/boxmain"
	"ThroneCore/internal/process"
	"ThroneCore/internal/sys"
	"ThroneCore/internal/wg"
	"ThroneCore/internal/xray"
	"ThroneCore/internal/xraydns"
	"ThroneCore/test_utils"
	"archive/zip"
	"context"
	"errors"
	"fmt"
	"io"
	"log"
	"net/netip"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/gofrs/uuid/v5"
	"github.com/google/shlex"
	"github.com/sagernet/sing-box/adapter"
	"github.com/sagernet/sing-box/common/trafficcontrol"
	"github.com/sagernet/sing/common/control"
	E "github.com/sagernet/sing/common/exceptions"
	"github.com/sagernet/sing/service"
	"github.com/xtls/xray-core/core"
	xinternet "github.com/xtls/xray-core/transport/internet"
)

// Serializes Start against Stop: the dispatcher gives every request its own goroutine.
var lifecycleMu sync.Mutex

// Guards the instance pointers; never held across a Create/Start, so pollers do not block behind a profile start.
var stateMu sync.RWMutex

var boxInstance *boxbox.Box
var instanceCancel context.CancelFunc

// Exactly one is set while a profile runs: xrayInstance when eager, xrayGate when the profile asked it to stay cold.
var xrayInstance *core.Instance
var xrayGate *xray.Gate

// One gate per opaque full config; never merged into the sidecar above.
var xrayFullGates []*xray.Gate

// Reached only from Start/Stop, i.e. always under lifecycleMu.
var extraProcess *process.Process
var needUnsetDNS bool
var debug bool

var errInstanceNotRunning = errors.New("Instance is not running")

func currentBox() *boxbox.Box {
	stateMu.RLock()
	defer stateMu.RUnlock()
	return boxInstance
}

func currentInstance() (*boxbox.Box, context.CancelFunc) {
	stateMu.RLock()
	defer stateMu.RUnlock()
	return boxInstance, instanceCancel
}

func setBoxInstance(box *boxbox.Box, cancel context.CancelFunc) {
	stateMu.Lock()
	defer stateMu.Unlock()
	boxInstance, instanceCancel = box, cancel
}

func setXray(instance *core.Instance, gate *xray.Gate) {
	stateMu.Lock()
	defer stateMu.Unlock()
	xrayInstance, xrayGate = instance, gate
}

func setXrayFullGates(gates []*xray.Gate) {
	stateMu.Lock()
	defer stateMu.Unlock()
	xrayFullGates = gates
}

// Shorter than the profile's config list: a gated instance is absent between activations.
func liveXrayInstances() []*core.Instance {
	stateMu.RLock()
	instance, gate := xrayInstance, xrayGate
	fullGates := xrayFullGates
	stateMu.RUnlock()

	var instances []*core.Instance
	if instance != nil {
		instances = append(instances, instance)
	} else if gate != nil {
		if live := gate.Instance(); live != nil {
			instances = append(instances, live)
		}
	}
	for _, fullGate := range fullGates {
		if live := fullGate.Instance(); live != nil {
			instances = append(instances, live)
		}
	}
	return instances
}

type server struct {
	gen.UnimplementedLibcoreServiceServer
}

func To[T any](v T) *T {
	return &v
}

func init() {
	m := boxdns.DnsManagerInstance
	if m == nil || m.Monitor == nil {
		return
	}
	m.Monitor.RegisterCallback(func(ifc *control.Interface, _ int) {
		name := ""
		if ifc != nil {
			name = ifc.Name
		}
		// The callback's interface is fresher than currentEgress would report here; the mark carries over unchanged.
		for _, inst := range liveXrayInstances() {
			inst.SetEgress(name, autoRedirectMark.Load())
		}
	})
}

// On failure the started ones are torn down; on success the caller must close them.
func startXrayFullConfigs(configs []string, prepare func(*core.Instance) error) ([]*core.Instance, error) {
	instances := make([]*core.Instance, 0, len(configs))
	for _, cfg := range configs {
		inst, err := xray.CreateXrayInstance(cfg)
		if err != nil {
			closeXrayInstances(instances)
			return nil, err
		}
		if err := prepare(inst); err != nil {
			_ = inst.Close()
			closeXrayInstances(instances)
			return nil, err
		}
		if err := inst.Start(); err != nil {
			_ = inst.Close()
			closeXrayInstances(instances)
			return nil, err
		}
		instances = append(instances, inst)
	}
	return instances, nil
}

// Slots are cleared before the close so no reader picks up a pointer that is going away.
func closeXray() {
	stateMu.Lock()
	instance, gate := xrayInstance, xrayGate
	fullGates := xrayFullGates
	xrayInstance, xrayGate, xrayFullGates = nil, nil, nil
	stateMu.Unlock()

	if gate != nil {
		gate.Close()
	}
	for _, fullGate := range fullGates {
		fullGate.Close()
	}
	if instance != nil {
		instance.Close()
	}
}

// On failure the gates already opened are torn down; on success closeXray owns them.
func startXrayFullGates(configs []string, idle time.Duration, prepare func(*core.Instance) error) ([]*xray.Gate, error) {
	if len(configs) == 0 {
		return nil, nil
	}
	gates := make([]*xray.Gate, len(configs))
	errs := make([]error, len(configs))

	unwind := func() {
		for _, opened := range gates {
			if opened != nil {
				opened.Close()
			}
		}
	}

	// Alone, not in the fan-out below: the first config loads the geo tables the rest share, so parallelizing races to load them all.
	gates[0], errs[0] = xray.StartGate(configs[0], idle, prepare)
	if errs[0] != nil {
		return nil, errs[0]
	}

	workers := runtime.NumCPU()
	if workers < 1 {
		workers = 1
	}
	slots := make(chan struct{}, workers)
	var wg sync.WaitGroup
	for i := 1; i < len(configs); i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			slots <- struct{}{}
			defer func() { <-slots }()
			gates[i], errs[i] = xray.StartGate(configs[i], idle, prepare)
		}()
	}
	wg.Wait()

	for _, err := range errs {
		if err == nil {
			continue
		}
		unwind()
		return nil, err
	}
	return gates, nil
}

// One per start, never a package global: a probe box must not answer for the running instance.
type boxContextHolder struct {
	ctx atomic.Pointer[context.Context]
}

func (h *boxContextHolder) publish(box *boxbox.Box) {
	if box == nil {
		return
	}
	boxCtx := box.Context()
	h.ctx.Store(&boxCtx)
}

func (h *boxContextHolder) get() context.Context {
	if stored := h.ctx.Load(); stored != nil {
		return *stored
	}
	return nil
}

// Must run between core.New and Start; both settings keep the instance off an active TUN.
func xrayPreparer(dnsStrategy string, boxCtx xraydns.BoxProvider) func(*core.Instance) error {
	return func(instance *core.Instance) error {
		instance.SetEgress(currentEgress())
		if dnsStrategy == "" || boxCtx == nil {
			return nil
		}
		// One resolver per instance: it caches the dns-direct transport of the box it was prepared for.
		instance.SetOutboundDNS(xraydns.New(boxCtx), xinternet.ParseDomainStrategy(dnsStrategy))
		return nil
	}
}

func closeXrayInstances(instances []*core.Instance) {
	for _, inst := range instances {
		_ = inst.Close()
	}
}

type testEnv struct {
	box   *boxbox.Box
	tags  []string
	close func()
}

// `current` measures the running instance instead of building one, and owns nothing.
func prepareTestEnv(current bool, needXray bool, xrayConfig string, xrayFullConfigs []string,
	coreConfig string, tags []string, useDefaultOutbound bool,
	xrayDNSStrategy string) (*testEnv, error) {

	// Owned here, not by the caller: this builds the probe box the Xray instances below resolve through.
	var boxCtx boxContextHolder
	prepareXray := xrayPreparer(xrayDNSStrategy, boxCtx.get)

	if current {
		box := currentBox()
		if box == nil {
			return nil, errInstanceNotRunning
		}
		outTags := tags
		if _, exists := box.Outbound().Outbound("proxy"); exists {
			outTags = []string{"proxy"}
		} else {
			useDefaultOutbound = true
		}
		if useDefaultOutbound {
			outTags = []string{box.Outbound().Default().Tag()}
		}
		return &testEnv{box: box, tags: outTags, close: func() {}}, nil
	}

	var cleanups []func()
	unwind := func() {
		for i := len(cleanups) - 1; i >= 0; i-- {
			cleanups[i]()
		}
	}

	if needXray {
		instance, err := xray.CreateXrayInstance(xrayConfig)
		if err != nil {
			unwind()
			return nil, err
		}
		if err = prepareXray(instance); err != nil {
			_ = instance.Close()
			unwind()
			return nil, err
		}
		if err = instance.Start(); err != nil {
			_ = instance.Close()
			unwind()
			return nil, err
		}
		cleanups = append(cleanups, func() { _ = instance.Close() })
	}

	fullXray, err := startXrayFullConfigs(xrayFullConfigs, prepareXray)
	if err != nil {
		unwind()
		return nil, err
	}
	cleanups = append(cleanups, func() { closeXrayInstances(fullXray) })

	box, cancel, err := boxmain.Create([]byte(coreConfig), boxCtx.publish)
	if err != nil {
		unwind()
		return nil, err
	}
	cleanups = append(cleanups, func() {
		box.CloseWithTimeout(cancel, 2*time.Second, log.Println, false)
	})

	outTags := tags
	if useDefaultOutbound {
		outTags = []string{box.Outbound().Default().Tag()}
	}
	return &testEnv{box: box, tags: outTags, close: unwind}, nil
}

func speedTestResultToProto(res test_utils.SpeedTestResult) *gen.SpeedTestResult {
	errStr := ""
	if res.Error != nil {
		errStr = res.Error.Error()
	}
	return &gen.SpeedTestResult{
		DlSpeed:       To(res.DlSpeed),
		UlSpeed:       To(res.UlSpeed),
		Latency:       To(res.Latency),
		OutboundTag:   To(res.Tag),
		Error:         To(errStr),
		ServerName:    To(res.ServerName),
		ServerCountry: To(res.ServerCountry),
		Cancelled:     To(res.Cancelled),
		DlBytes:       To(res.DlBytes),
		UlBytes:       To(res.UlBytes),
	}
}

func (s *server) Start(ctx context.Context, in *gen.LoadConfigReq) (out *gen.ErrorResp, _ error) {
	lifecycleMu.Lock()
	defer lifecycleMu.Unlock()

	var err error

	defer func() {
		out = &gen.ErrorResp{}
		if err != nil {
			out.Error = To(err.Error())
			setBoxInstance(nil, nil)
			autoRedirectMark.Store(0)
		}
	}()

	if debug {
		log.Println("Start:", *in.CoreConfig)
		if in.XrayConfig != nil {
			log.Println("Start Xray:", *in.XrayConfig)
		}
	}

	if currentBox() != nil {
		err = errors.New("instance already started")
		return
	}

	if *in.NeedExtraProcess {
		args, e := shlex.Split(in.GetExtraProcessArgs())
		if e != nil {
			err = E.Cause(e, "Failed to parse args")
			return
		}
		var extraConfPath, extraCleanupPath string
		if in.ExtraProcessConf != nil {
			extraConfPath, extraCleanupPath, e = process.CreateExtraConfig(*in.ExtraProcessConf)
			if e != nil {
				err = E.Cause(e, "Failed to create extra.conf")
				return
			}
			for idx, arg := range args {
				if strings.Contains(arg, "%s") {
					args[idx] = fmt.Sprintf(arg, extraConfPath)
					break
				}
			}
		}

		extraProcess = process.NewProcess(*in.ExtraProcessPath, args, *in.ExtraNoOut)
		extraProcess.SetCleanupPath(extraCleanupPath)
		err = extraProcess.Start()
		if err != nil {
			return
		}
	}

	autoRedirectMark.Store(autoRedirectMarkFor([]byte(in.GetCoreConfig())))

	// Filled in below, once boxmain.Create has built the box these sidecars resolve through.
	var boxCtx boxContextHolder
	prepareXray := xrayPreparer(in.GetXrayOutboundDnsStrategy(), boxCtx.get)

	if *in.NeedXray {
		if in.GetXrayLazyStart() {
			gate, e := xray.StartGate(*in.XrayConfig,
				time.Duration(in.GetXrayIdleSeconds())*time.Second, prepareXray)
			if e != nil {
				err = e
				return
			}
			setXray(nil, gate)
		} else {
			instance, e := xray.CreateXrayInstance(*in.XrayConfig)
			if e != nil {
				err = e
				return
			}
			if e = prepareXray(instance); e != nil {
				instance.Close()
				err = e
				return
			}
			if e = instance.Start(); e != nil {
				instance.Close()
				err = e
				return
			}
			setXray(instance, nil)
		}
	}

	if fullConfigs := in.GetXrayFullConfigs(); len(fullConfigs) > 0 {
		gates, e := startXrayFullGates(fullConfigs,
			time.Duration(in.GetXrayFullIdleSeconds())*time.Second, prepareXray)
		if e != nil {
			closeXray()
			err = e
			return
		}
		setXrayFullGates(gates)
	}

	box, cancel, err := boxmain.Create([]byte(*in.CoreConfig), boxCtx.publish)
	if err != nil {
		if extraProcess != nil {
			extraProcess.Stop()
			extraProcess = nil
		}
		closeXray()
		return
	}
	setBoxInstance(box, cancel)

	if runtime.GOOS == "darwin" && in.GetTunIpv4Cidr() != "" {
		stopAllCores := func() {
			box.CloseWithTimeout(cancel, time.Second*2, log.Println, true)
			setBoxInstance(nil, nil)
			if extraProcess != nil {
				extraProcess.Stop()
				extraProcess = nil
			}
			closeXray()
		}

		tunCIDR := in.GetTunIpv4Cidr()
		tunPrefix, parseErr := netip.ParsePrefix(tunCIDR)
		if parseErr != nil || !tunPrefix.Addr().Is4() {
			err = fmt.Errorf("invalid tun_ipv4_cidr %q", tunCIDR)
			stopAllCores()
			return
		}

		tunDNS := tunPrefix.Addr()
		if !tunDNS.IsValid() || !tunDNS.Is4() {
			err = fmt.Errorf("got invalid DNS IP from tun_ipv4_cidr: %s", tunDNS)
			stopAllCores()
			return
		}

		if err := sys.SetSystemDNS(tunDNS.String(), box.Network().InterfaceMonitor()); err != nil {
			log.Println("Failed to set system DNS:", err)
		}

		needUnsetDNS = true
	}

	return
}

func (s *server) Stop(ctx context.Context, in *gen.EmptyReq) (out *gen.ErrorResp, _ error) {
	lifecycleMu.Lock()
	defer lifecycleMu.Unlock()

	var err error

	defer func() {
		out = &gen.ErrorResp{}
		if err != nil {
			out.Error = To(err.Error())
		}
	}()

	box, cancel := currentInstance()
	if box == nil {
		return
	}

	if needUnsetDNS {
		needUnsetDNS = false
		err := sys.SetSystemDNS("Empty", box.Network().InterfaceMonitor())
		if err != nil {
			log.Println("Failed to unset system DNS:", err)
		}
	}
	// Unpublished first, so a poll mid-teardown sees no instance rather than a dying one.
	setBoxInstance(nil, nil)
	box.CloseWithTimeout(cancel, time.Second*2, log.Println, true)

	if extraProcess != nil {
		extraProcess.Stop()
		extraProcess = nil
	}

	closeXray()
	// The Tun and its nftables rules went down with the box, so later test instances must not carry the exemption mark.
	autoRedirectMark.Store(0)

	return
}

func (s *server) CheckConfig(ctx context.Context, in *gen.LoadConfigReq) (out *gen.ErrorResp, _ error) {
	out = &gen.ErrorResp{}
	// boxmain.Check can panic on malformed configs; unrecovered it reaches main()'s os.Exit(0) and kills the core.
	defer func() {
		if r := recover(); r != nil {
			buf := make([]byte, 4096)
			n := runtime.Stack(buf, false)
			log.Printf("CheckConfig panic: %v\n%s", r, buf[:n])
			out.Error = To(fmt.Sprintf("CheckConfig panic: %v", r))
		}
	}()
	if in.GetNeedXray() {
		if err := xray.CheckXrayConfig(in.GetXrayConfig()); err != nil {
			out.Error = To(err.Error())
		}
		return
	}
	err := boxmain.Check([]byte(*in.CoreConfig))
	if err != nil {
		out.Error = To(err.Error())
	}
	return
}

func (s *server) Test(ctx context.Context, in *gen.TestReq) (*gen.TestResp, error) {
	env, err := prepareTestEnv(in.GetTestCurrent(), in.GetNeedXray(), in.GetXrayConfig(),
		in.XrayFullConfigs, in.GetConfig(), in.OutboundTags, in.GetUseDefaultOutbound(),
		in.GetXrayOutboundDnsStrategy())
	if err != nil {
		if errors.Is(err, errInstanceNotRunning) {
			return &gen.TestResp{Results: []*gen.URLTestResp{{
				OutboundTag: To("proxy"),
				LatencyMs:   To(int32(0)),
				Error:       To(err.Error()),
			}}}, nil
		}
		return nil, err
	}
	defer env.close()

	// Held, not re-read: StopTest rearms a fresh context, uncancelled.
	testCtx := test_utils.TestContext()

	// A muxed config needs a warm connection; the live instance already is one.
	twice := !in.GetTestCurrent()
	results := test_utils.BatchURLTest(testCtx, env.box, env.tags, in.GetUrl(),
		int(in.GetMaxConcurrency()), twice, time.Duration(in.GetTestTimeoutMs())*time.Millisecond)

	res := make([]*gen.URLTestResp, 0, len(results))
	failed := make(map[string]bool, len(results))
	for idx, data := range results {
		errStr := ""
		if data.Error != nil {
			errStr = data.Error.Error()
		}
		failed[env.tags[idx]] = errStr != ""
		res = append(res, &gen.URLTestResp{
			OutboundTag: To(env.tags[idx]),
			LatencyMs:   To(int32(data.Duration.Milliseconds())),
			Error:       To(errStr),
		})
	}

	out := &gen.TestResp{Results: res}
	// `defer env.close()` tears the box down on return, so this cannot wait.
	var pending []string
	for _, tag := range in.VpnEndpointTags {
		if failed[tag] {
			pending = append(pending, tag)
		}
	}
	if len(pending) > 0 {
		timeout := defaultVPNStatusTimeout
		if ms := in.GetVpnStatusTimeoutMs(); ms > 0 {
			timeout = time.Duration(ms) * time.Millisecond
		}
		out.VpnStatus = collectVPNStatus(testCtx, env.box, pending, timeout)
	}
	return out, nil
}

func (s *server) StopTest(ctx context.Context, in *gen.EmptyReq) (*gen.EmptyResp, error) {
	test_utils.CancelTests()

	return &gen.EmptyResp{}, nil
}

func (s *server) QueryURLTest(ctx context.Context, in *gen.EmptyReq) (out *gen.QueryURLTestResponse, _ error) {
	results := test_utils.URLReporter.Results()
	out = &gen.QueryURLTestResponse{}
	for _, r := range results {
		errStr := ""
		if r.Error != nil {
			errStr = r.Error.Error()
		}
		out.Results = append(out.Results, &gen.URLTestResp{
			OutboundTag: To(r.Tag),
			LatencyMs:   To(int32(r.Duration.Milliseconds())),
			Error:       To(errStr),
		})
	}
	return
}

func (s *server) IPTest(ctx context.Context, in *gen.IPTestRequest) (*gen.IPTestResp, error) {
	// Always builds its own box: there is no test-current variant of an IP test.
	env, err := prepareTestEnv(false, in.GetNeedXray(), in.GetXrayConfig(),
		in.XrayFullConfigs, in.GetConfig(), in.OutboundTags, in.GetUseDefaultOutbound(),
		in.GetXrayOutboundDnsStrategy())
	if err != nil {
		return nil, err
	}
	defer env.close()

	timeout := time.Duration(in.GetTestTimeoutMs()) * time.Millisecond
	results := test_utils.BatchIPTest(test_utils.TestContext(), env.box, env.tags,
		int(in.GetMaxConcurrency()), timeout)

	res := make([]*gen.IPTestRes, 0, len(results))
	for idx, data := range results {
		errStr := ""
		if data.Error != nil {
			errStr = data.Error.Error()
		}
		res = append(res, &gen.IPTestRes{
			OutboundTag: To(env.tags[idx]),
			Ip:          To(data.Result.IP),
			CountryCode: To(data.Result.CountryCode),
			Error:       To(errStr),
		})
	}
	return &gen.IPTestResp{Results: res}, nil
}

func (s *server) QueryIPTest(ctx context.Context, in *gen.EmptyReq) (out *gen.QueryIPTestResponse, _ error) {
	results := test_utils.IPReporter.Results()
	out = &gen.QueryIPTestResponse{}
	for _, r := range results {
		errStr := ""
		if r.Error != nil {
			errStr = r.Error.Error()
		}
		out.Results = append(out.Results, &gen.IPTestRes{
			OutboundTag: To(r.Tag),
			Ip:          To(r.Result.IP),
			CountryCode: To(r.Result.CountryCode),
			Error:       To(errStr),
		})
	}
	return
}

func (s *server) QueryStats(ctx context.Context, in *gen.EmptyReq) (out *gen.QueryStatsResp, err error) {
	out = &gen.QueryStatsResp{}
	out.Ups = make(map[string]int64)
	out.Downs = make(map[string]int64)
	if box := currentBox(); box != nil {
		trafficManager := service.PtrFromContext[trafficcontrol.Manager](box.Context())
		if trafficManager != nil {
			outbounds := service.FromContext[adapter.OutboundManager](box.Context())
			if outbounds == nil {
				log.Println("Failed to get outbound manager")
				err = E.New("nil outbound manager")
				return
			}
			endpoints := service.FromContext[adapter.EndpointManager](box.Context())
			if endpoints == nil {
				log.Println("Failed to get endpoint manager")
				err = E.New("nil endpoint manager")
				return
			}
			for _, ob := range outbounds.Outbounds() {
				u, d := trafficManager.TotalOutbound(ob.Tag())
				out.Ups[ob.Tag()] = u
				out.Downs[ob.Tag()] = d
			}
			for _, ep := range endpoints.Endpoints() {
				u, d := trafficManager.TotalOutbound(ep.Tag())
				out.Ups[ep.Tag()] = u
				out.Downs[ep.Tag()] = d
			}
		}
	}
	return
}

func connMetaToProto(c *trafficcontrol.TrackerMetadata) *gen.ConnectionMetaData {
	process := ""
	processPath := ""
	if c.Metadata.ProcessInfo != nil {
		processPath = c.Metadata.ProcessInfo.ProcessPath
		spl := strings.Split(processPath, string(os.PathSeparator))
		process = spl[len(spl)-1]
	}
	var closedAt int64
	if !c.ClosedAt.IsZero() {
		closedAt = c.ClosedAt.UnixMilli()
	}
	source := ""
	if c.Metadata.Source.IsValid() {
		source = c.Metadata.Source.String()
	}
	return &gen.ConnectionMetaData{
		Id:          To(c.ID.String()),
		CreatedAt:   To(c.CreatedAt.UnixMilli()),
		Upload:      To(c.Upload.Load()),
		Download:    To(c.Download.Load()),
		Outbound:    To(c.Outbound),
		Network:     To(c.Metadata.Network),
		Dest:        To(c.Metadata.Destination.String()),
		Protocol:    To(c.Metadata.Protocol),
		Domain:      To(c.Metadata.Domain),
		Process:     To(process),
		ProcessPath: To(processPath),
		Chain:       c.Chain,
		ClosedAt:    To(closedAt),
		Source:      To(source),
	}
}

// Non-draining: the recently-closed ring is re-reported every poll and the client dedups by id.
func (s *server) QueryConnections(ctx context.Context, in *gen.EmptyReq) (*gen.QueryConnectionsResp, error) {
	box := currentBox()
	if box == nil {
		return &gen.QueryConnectionsResp{}, nil
	}
	tm := service.PtrFromContext[trafficcontrol.Manager](box.Context())
	if tm == nil {
		return &gen.QueryConnectionsResp{}, errors.New("no traffic manager found")
	}

	active := make([]*gen.ConnectionMetaData, 0)
	for _, c := range tm.Connections() {
		active = append(active, connMetaToProto(c))
	}
	closed := make([]*gen.ConnectionMetaData, 0)
	for _, c := range tm.ClosedConnections() {
		closed = append(closed, connMetaToProto(c))
	}
	return &gen.QueryConnectionsResp{Active: active, Closed: closed}, nil
}

// Ids that already closed are a silent no-op: the client's table is always a poll behind.
func (s *server) CloseConnections(ctx context.Context, in *gen.CloseConnectionsRequest) (*gen.CloseConnectionsResponse, error) {
	if len(in.Ids) == 0 {
		return &gen.CloseConnectionsResponse{Closed: To(int32(0))}, nil
	}
	box := currentBox()
	if box == nil {
		return &gen.CloseConnectionsResponse{Error: To("no instance is running")}, nil
	}
	tm := service.PtrFromContext[trafficcontrol.Manager](box.Context())
	if tm == nil {
		return &gen.CloseConnectionsResponse{Error: To("no traffic manager found")}, nil
	}

	var closed int32
	for _, raw := range in.Ids {
		id, err := uuid.FromString(raw)
		if err != nil {
			continue
		}
		tracker := tm.Connection(id)
		if tracker == nil {
			continue
		}
		tracker.Close() //nolint:errcheck — the tracker leaves the manager either way
		closed++
	}
	return &gen.CloseConnectionsResponse{Closed: To(closed)}, nil
}

func (s *server) IsPrivileged(ctx context.Context, _ *gen.EmptyReq) (*gen.IsPrivilegedResponse, error) {
	if runtime.GOOS == "windows" {
		return &gen.IsPrivilegedResponse{
			HasPrivilege: To(false),
		}, nil
	}

	return &gen.IsPrivilegedResponse{HasPrivilege: To(os.Geteuid() == 0)}, nil
}

func (s *server) SpeedTest(ctx context.Context, in *gen.SpeedTestRequest) (*gen.SpeedTestResponse, error) {
	if !*in.TestDownload && !*in.TestUpload && !*in.SimpleDownload && !*in.OnlyCountry {
		return nil, errors.New("cannot run empty test")
	}

	env, err := prepareTestEnv(in.GetTestCurrent(), in.GetNeedXray(), in.GetXrayConfig(),
		in.XrayFullConfigs, in.GetConfig(), in.OutboundTags, in.GetUseDefaultOutbound(),
		in.GetXrayOutboundDnsStrategy())
	if err != nil {
		if errors.Is(err, errInstanceNotRunning) {
			return &gen.SpeedTestResponse{Results: []*gen.SpeedTestResult{{
				OutboundTag: To("proxy"),
				Error:       To(err.Error()),
			}}}, nil
		}
		return nil, err
	}
	defer env.close()

	results := test_utils.BatchSpeedTest(test_utils.TestContext(), env.box, env.tags,
		*in.TestDownload, *in.TestUpload, *in.SimpleDownload, *in.SimpleDownloadAddr,
		time.Duration(*in.TimeoutMs)*time.Millisecond, *in.OnlyCountry, *in.CountryConcurrency)

	res := make([]*gen.SpeedTestResult, 0, len(results))
	for _, data := range results {
		res = append(res, speedTestResultToProto(*data))
	}

	return &gen.SpeedTestResponse{Results: res}, nil
}

func (s *server) QuerySpeedTest(context.Context, *gen.EmptyReq) (*gen.QuerySpeedTestResponse, error) {
	res, isRunning := test_utils.SpTQuerier.Result()
	return &gen.QuerySpeedTestResponse{
		Result:    speedTestResultToProto(res),
		IsRunning: To(isRunning),
	}, nil
}

func (s *server) QueryCountryTest(ctx context.Context, _ *gen.EmptyReq) (out *gen.QueryCountryTestResponse, _ error) {
	results := test_utils.CountryResults.Results()
	out = &gen.QueryCountryTestResponse{}
	for _, res := range results {
		out.Results = append(out.Results, speedTestResultToProto(*res))
	}
	return
}

func (s *server) GenWgKeyPair(ctx context.Context, _ *gen.EmptyReq) (out *gen.GenWgKeyPairResponse, _ error) {
	var res gen.GenWgKeyPairResponse
	privateKey, err := wg.GeneratePrivateKey()
	if err != nil {
		res.Error = To(err.Error())
		return &res, nil
	}
	res.PrivateKey = To(privateKey.String())
	res.PublicKey = To(privateKey.PublicKey().String())
	return &res, nil
}

func (s *server) InstallDashboard(ctx context.Context, in *gen.InstallDashboardRequest) (*gen.ErrorResp, error) {
	archivePath := in.GetArchivePath()
	targetDir := in.GetTargetDir()
	if archivePath == "" || targetDir == "" {
		return &gen.ErrorResp{Error: To("missing archive path or target dir")}, nil
	}

	reader, err := zip.OpenReader(archivePath)
	if err != nil {
		return &gen.ErrorResp{Error: To(E.Cause(err, "open dashboard archive").Error())}, nil
	}
	defer reader.Close()

	trimDir := zipIsInSingleDirectory(reader.File)
	tempDir := targetDir + ".tmp"
	if err = os.RemoveAll(tempDir); err != nil {
		return &gen.ErrorResp{Error: To(err.Error())}, nil
	}
	if err = os.MkdirAll(tempDir, 0o755); err != nil {
		return &gen.ErrorResp{Error: To(err.Error())}, nil
	}

	failed := func(err error) (*gen.ErrorResp, error) {
		os.RemoveAll(tempDir)
		return &gen.ErrorResp{Error: To(err.Error())}, nil
	}
	var extracted int
	for _, file := range reader.File {
		if file.FileInfo().IsDir() {
			continue
		}
		elements := strings.Split(file.Name, "/")
		if trimDir {
			elements = elements[1:]
		}
		if len(elements) == 0 {
			continue
		}
		relativePath := filepath.Join(elements...)
		if !filepath.IsLocal(relativePath) {
			return failed(E.New("invalid dashboard archive entry: ", file.Name))
		}
		savePath := filepath.Join(tempDir, relativePath)
		if err = os.MkdirAll(filepath.Dir(savePath), 0o755); err != nil {
			return failed(err)
		}
		if err = extractZipEntry(file, savePath); err != nil {
			return failed(err)
		}
		extracted++
	}
	if extracted == 0 {
		return failed(E.New("dashboard archive is empty"))
	}
	if err = adoptExtracted(tempDir); err != nil {
		return failed(E.Cause(err, "hand the dashboard files to the invoking user"))
	}

	if err = os.RemoveAll(targetDir); err != nil {
		return failed(err)
	}
	if err = os.Rename(tempDir, targetDir); err != nil {
		return failed(err)
	}
	return &gen.ErrorResp{}, nil
}

func extractZipEntry(file *zip.File, savePath string) error {
	reader, err := file.Open()
	if err != nil {
		return err
	}
	defer reader.Close()
	writer, err := os.Create(savePath)
	if err != nil {
		return err
	}
	defer writer.Close()
	_, err = io.Copy(writer, reader)
	return err
}

// GitHub archives wrap every file under a single "<repo>-<branch>/" top-level directory.
func zipIsInSingleDirectory(files []*zip.File) bool {
	var dirName string
	for _, file := range files {
		if file.FileInfo().IsDir() {
			continue
		}
		elements := strings.Split(file.Name, "/")
		if len(elements) < 2 {
			return false
		}
		if dirName == "" {
			dirName = elements[0]
		} else if dirName != elements[0] {
			return false
		}
	}
	return dirName != ""
}
