package tools

import "github.com/diamondburned/gotk4/pkg/gtk/v4"

func ScrollableBox(box *gtk.Box) *gtk.ScrolledWindow {
	viewport := gtk.NewViewport(nil, nil)
	viewport.SetScrollToFocus(true)
	viewport.SetChild(box)

	scrolledWindow := gtk.NewScrolledWindow()
	scrolledWindow.SetPolicy(gtk.PolicyNever, gtk.PolicyAutomatic)
	scrolledWindow.SetChild(viewport)
	scrolledWindow.SetPropagateNaturalHeight(true)

	return scrolledWindow
}
