package main

import (
	"github.com/diamondburned/gotk4/pkg/gio/v2"
	"github.com/diamondburned/gotk4/pkg/gtk/v4"
	notebook2 "github.com/edjubert/hyprland-share-picker-gtk/src/pkg/notebook"
	"os"
)

func main() {
	app := gtk.NewApplication("hyprland.share.picker", gio.ApplicationFlagsNone)
	app.ConnectActivate(func() { activate(app) })

	if code := app.Run(os.Args); code > 0 {
		os.Exit(code)
	}
}

func activate(app *gtk.Application) {
	window := gtk.NewApplicationWindow(app)

	notebook := notebook2.CreateNotebook()

	window.SetChild(notebook)
	window.SetDefaultSize(400, 300)
	window.Show()
}
