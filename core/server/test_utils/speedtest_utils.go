package test_utils

import (
	"context"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"sync"
	"sync/atomic"
	"time"

	"ThroneCore/internal"
	"ThroneCore/internal/boxbox"

	"github.com/sagernet/sing-box/adapter"
	"github.com/sagernet/sing/service"
)

var SpTQuerier SpeedTestResultQuerier
var CountryResults resultBuffer[SpeedTestResult]

const speedtestSampleInterval = 50 * time.Millisecond

type SpeedTestResult struct {
	Tag           string
	DlSpeed       string
	UlSpeed       string
	Latency       int32
	ServerName    string
	ServerCountry string
	Error         error
	Cancelled     bool
	DlBytes       int64 // credited to per-config traffic stats
	UlBytes       int64
}

type SpeedTestResultQuerier struct {
	isRunning bool
	current   SpeedTestResult
	mu        sync.RWMutex
}

func (s *SpeedTestResultQuerier) Result() (SpeedTestResult, bool) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.current, s.isRunning
}

func (s *SpeedTestResultQuerier) storeResult(result *SpeedTestResult) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.current = *result
}

func (s *SpeedTestResultQuerier) setIsRunning(isRunning bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.isRunning = isRunning
}

// The sampling loop reads the total mid-copy, which a bytes.Buffer cannot serve safely.
type countingWriter struct{ n atomic.Int64 }

func (w *countingWriter) Write(p []byte) (int, error) {
	w.n.Add(int64(len(p)))
	return len(p), nil
}

func countryTest(ctx context.Context, dialer func(ctx context.Context, network string, address string) (net.Conn, error), res *SpeedTestResult) error {
	srv, closeClient, err := getSpeedtestServer(ctx, dialer)
	if err != nil {
		return err
	}
	defer closeClient()
	res.ServerName = srv.Name
	res.ServerCountry = srv.Country
	res.Latency = int32(srv.Latency.Milliseconds())
	return nil
}

func BatchSpeedTest(ctx context.Context, i *boxbox.Box, outboundTags []string, testDl, testUl bool, simpleDL bool, simpleAddress string, timeout time.Duration, countryOnly bool, countryConcurrency int32) []*SpeedTestResult {
	outbounds := service.FromContext[adapter.OutboundManager](i.Context())
	results := make([]*SpeedTestResult, 0, len(outboundTags))
	var queuer chan struct{}
	wg := &sync.WaitGroup{}
	if countryOnly {
		if countryConcurrency <= 0 {
			countryConcurrency = 5
		}
		queuer = make(chan struct{}, countryConcurrency)
	}

	for _, tag := range outboundTags {
		// A plain `break` here would leave the select, not the loop.
		if ctx.Err() != nil {
			break
		}

		res := new(SpeedTestResult)
		res.Tag = tag
		results = append(results, res)

		outbound, exists := outbounds.Outbound(tag)
		if !exists {
			// Report rather than panic: a tag can vanish between building the box and testing.
			res.Error = fmt.Errorf("no outbound with tag %s found", tag)
			if countryOnly {
				CountryResults.AddResult(res)
			}
			continue
		}

		var err error
		if countryOnly {
			queuer <- struct{}{}
			wg.Add(1)
			go func(res *SpeedTestResult, outbound adapter.Outbound) {
				defer func() { <-queuer }()
				defer wg.Done()
				err := countryTest(ctx, getNetDialer(outbound.DialContext), res)
				if err != nil && !errors.Is(err, context.Canceled) {
					res.Error = err
					fmt.Println("Failed to countryTest with err:", err)
				}
				CountryResults.AddResult(res)
			}(res, outbound)
			continue
		}
		if simpleDL {
			err = simpleDownloadTest(ctx, getNetDialer(outbound.DialContext), res, simpleAddress, timeout)
		} else {
			err = speedTestWithDialer(ctx, getNetDialer(outbound.DialContext), res, testDl, testUl, timeout)
		}
		if err != nil && !errors.Is(err, context.Canceled) {
			res.Error = err
			fmt.Println("Failed to speedtest with err:", err)
		}
		if !testDl && !simpleDL {
			res.DlSpeed = ""
		}
		if !testUl {
			res.UlSpeed = ""
		}
	}
	wg.Wait()
	CountryResults.Reclaim(results)

	return results
}

func simpleDownloadTest(ctx context.Context, dialer func(ctx context.Context, network string, address string) (net.Conn, error), res *SpeedTestResult, testURL string, timeout time.Duration) error {
	if timeout <= 0 {
		timeout = URLTestTimeout
	}
	client, closeClient := dialerHTTPClient(dialer, timeout)
	defer closeClient()

	res.ServerName = "N/A"
	res.ServerCountry = "N/A"

	req, err := http.NewRequestWithContext(ctx, "GET", testURL, nil)
	if err != nil {
		return err
	}

	done := make(chan struct{})
	// Written by the download goroutine while the sampling loop reads them.
	var counted countingWriter
	var startNs atomic.Int64
	var latencyMs atomic.Int32
	// Only read after <-done, which orders it against the goroutine's write.
	var downloadErr error

	go func() {
		defer close(done)
		reqStart := time.Now()
		resp, err := client.Do(req)
		if err != nil {
			downloadErr = err
			return
		}
		defer resp.Body.Close()
		latencyMs.Store(int32(time.Since(reqStart).Milliseconds()))
		startNs.Store(time.Now().UnixNano())
		_, _ = io.Copy(&counted, resp.Body)
	}()

	ticker := time.NewTicker(speedtestSampleInterval)
	defer ticker.Stop()

	SpTQuerier.setIsRunning(true)
	defer SpTQuerier.setIsRunning(false)

	sample := func() {
		n := counted.n.Load()
		if start := startNs.Load(); start != 0 {
			res.DlSpeed = internal.BrateToStr(internal.CalculateBRate(float64(n), time.Unix(0, start)))
		}
		res.DlBytes = n
		res.Latency = latencyMs.Load()
		SpTQuerier.storeResult(res)
	}

	for {
		select {
		case <-done:
			if downloadErr != nil {
				res.Error = downloadErr
			}
			sample()
			return downloadErr
		case <-ctx.Done():
			res.Cancelled = true
			return ctx.Err()
		case <-ticker.C:
			sample()
		}
	}
}

func speedTestWithDialer(ctx context.Context, dialer func(ctx context.Context, network string, address string) (net.Conn, error), res *SpeedTestResult, testDl, testUl bool, timeout time.Duration) error {
	srv, closeClient, err := getSpeedtestServer(ctx, dialer)
	if err != nil {
		return err
	}
	defer closeClient()
	res.ServerName = srv.Name
	res.ServerCountry = srv.Country

	done := make(chan struct{})
	// Only read after <-done, which orders it against the goroutine's write.
	var testErr error

	SpTQuerier.setIsRunning(true)
	defer SpTQuerier.setIsRunning(false)

	go func() {
		defer func() { close(done) }()
		if testDl {
			timeoutCtx, cancel := context.WithTimeout(ctx, timeout)
			defer cancel()
			if e := srv.DownloadTestContext(timeoutCtx); e != nil && !errors.Is(e, context.Canceled) {
				testErr = e
				return
			}
		}
		if testUl {
			timeoutCtx, cancel := context.WithTimeout(ctx, timeout)
			defer cancel()
			if e := srv.UploadTestContext(timeoutCtx); e != nil && !errors.Is(e, context.Canceled) {
				testErr = e
				return
			}
		}
	}()

	ticker := time.NewTicker(speedtestSampleInterval)
	defer ticker.Stop()

	for {
		select {
		case <-done:
			if testErr != nil {
				res.Error = testErr
			}
			res.DlSpeed = internal.BrateToStr(float64(srv.DLSpeed))
			res.UlSpeed = internal.BrateToStr(float64(srv.ULSpeed))
			res.DlBytes = srv.Context.GetTotalDownload()
			res.UlBytes = srv.Context.GetTotalUpload()
			res.Latency = int32(srv.Latency.Milliseconds())
			SpTQuerier.storeResult(res)
			return nil
		case <-ctx.Done():
			res.Cancelled = true
			return ctx.Err()
		case <-ticker.C:
			res.DlSpeed = internal.BrateToStr(srv.Context.GetEWMADownloadRate())
			res.UlSpeed = internal.BrateToStr(srv.Context.GetEWMAUploadRate())
			res.DlBytes = srv.Context.GetTotalDownload()
			res.UlBytes = srv.Context.GetTotalUpload()
			res.Latency = int32(srv.Latency.Milliseconds())
			SpTQuerier.storeResult(res)
		}
	}
}
