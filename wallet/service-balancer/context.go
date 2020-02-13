package main

import (
	"context"
	"os/exec"
)

const (
	CtiWalletID = iota
	CtiCommand
)

// TODO: get rid of the stuff below
func store(ctx context.Context, key interface{}, value interface{}) context.Context {
	if ctx == nil {
		ctx = context.Background()
	}
	return context.WithValue(ctx, key, value)
}

func getWID(ctx context.Context) string {
	if wid, ok := ctx.Value(CtiWalletID).(string); ok {
		return wid
	}
	panic("failed to get Wallet ID")
}

func storeWID(ctx context.Context, wid string) context.Context {
	return store(ctx, CtiWalletID, wid)
}

func getCmd(ctx context.Context) *exec.Cmd {
	if cmd, ok := ctx.Value(CtiCommand).(*exec.Cmd); ok {
		return cmd
	}
	panic("failed to get Cmd")
}
