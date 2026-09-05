package boxmain

import (
	"ThroneCore/internal/boxbox"
	"context"
	"os"
	"os/signal"
	runtimeDebug "runtime/debug"
	"syscall"
	"time"

	C "github.com/sagernet/sing-box/constant"
	"github.com/sagernet/sing-box/log"
	"github.com/sagernet/sing-box/option"
	E "github.com/sagernet/sing/common/exceptions"
	"github.com/sagernet/sing/common/json"
	"github.com/sagernet/sing/service/filemanager"
	"github.com/spf13/cobra"
)

var commandRun = &cobra.Command{
	Use:   "run",
	Short: "Run service",
	Run: func(cmd *cobra.Command, args []string) {
		err := run()
		if err != nil {
			log.Fatal(err)
		}
	},
}

func init() {
	mainCommand.AddCommand(commandRun)
}

type OptionsEntry struct {
	content []byte
	path    string
	options option.Options
}

func parseConfig(ctx context.Context, configContent []byte) (*option.Options, error) {
	var (
		err error
	)
	options, err := json.UnmarshalExtendedContext[option.Options](ctx, configContent)
	if err != nil {
		return nil, E.Cause(err, "decode config at ", string(configContent))
	}
	return &options, nil
}

// onCreated runs between New and Start: the Xray sidecars start first and resolve through this box.
func Create(configContent []byte, onCreated func(*boxbox.Box)) (*boxbox.Box, context.CancelFunc, error) {
	// Fresh context per call: concurrent boxes sharing one service.Registry clobber each other's OutboundManager.
	ctx := newBoxContext()
	options, err := parseConfig(ctx, configContent)
	if err != nil {
		return nil, nil, err
	}
	if disableColor {
		if options.Log == nil {
			options.Log = &option.LogOptions{}
		}
		options.Log.DisableColor = true
	}
	ctx, cancel := context.WithCancel(ctx)
	instance, err := boxbox.New(boxbox.Options{
		Context: ctx,
		Options: *options,
	})
	if err != nil {
		cancel()
		return nil, nil, E.Cause(err, "create service")
	}
	if onCreated != nil {
		onCreated(instance)
	}

	osSignals := make(chan os.Signal, 1)
	signal.Notify(osSignals, os.Interrupt, syscall.SIGTERM, syscall.SIGHUP)
	defer func() {
		signal.Stop(osSignals)
		close(osSignals)
	}()
	startCtx, finishStart := context.WithCancel(context.Background())
	go func() {
		_, loaded := <-osSignals
		if loaded {
			cancel()
			closeMonitor(startCtx)
		}
	}()
	err = instance.Start()
	finishStart()
	if err != nil {
		cancel()
		return nil, nil, E.Cause(err, "start service")
	}
	adoptCacheFile(ctx, options)
	return instance, cancel, nil
}

// filemanager.WithDefault compares the target against the real uid, which under setuid is already the user, so it never chowns.
func adoptCacheFile(ctx context.Context, options *option.Options) {
	if os.Geteuid() != 0 || os.Getuid() == 0 {
		return
	}
	experimental := options.Experimental
	if experimental == nil || experimental.CacheFile == nil || !experimental.CacheFile.Enabled {
		return
	}
	path := experimental.CacheFile.Path
	if path == "" {
		path = "cache.db"
	}
	path = filemanager.BasePath(ctx, path)
	if err := os.Lchown(path, os.Getuid(), os.Getgid()); err != nil {
		log.Warn(E.Cause(err, "chown cache file"))
	}
}

func run() error {
	osSignals := make(chan os.Signal, 1)
	signal.Notify(osSignals, os.Interrupt, syscall.SIGTERM)
	defer signal.Stop(osSignals)
	for {
		instance, cancel, err := Create([]byte{}, nil)
		if err != nil {
			return err
		}
		runtimeDebug.FreeOSMemory()
		for {
			osSignal := <-osSignals
			cancel()
			closeCtx, closed := context.WithCancel(context.Background())
			go closeMonitor(closeCtx)
			err = instance.Close()
			closed()
			if osSignal != syscall.SIGHUP {
				if err != nil {
					log.Error(E.Cause(err, "sing-box did not closed properly"))
				}
				return nil
			}
			break
		}
	}
}

func closeMonitor(ctx context.Context) {
	time.Sleep(C.FatalStopTimeout)
	select {
	case <-ctx.Done():
		return
	default:
	}
	log.Fatal("sing-box did not close!")
}
