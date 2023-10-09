package notebook

import (
	"github.com/diamondburned/gotk4/pkg/gtk/v4"
	"github.com/edjubert/hyprland-share-picker-gtk/src/pkg/region"
	"github.com/edjubert/hyprland-share-picker-gtk/src/pkg/screen"
	"github.com/edjubert/hyprland-share-picker-gtk/src/pkg/tools"
	"github.com/edjubert/hyprland-share-picker-gtk/src/pkg/window"
)

func CreateNotebook() *gtk.Notebook {
	notebook := gtk.NewNotebook()
	screenPage := tools.ScrollableBox(screen.CreatePage())
	windowPage := tools.ScrollableBox(window.CreateWindowPage())
	regionPage := region.CreateRegionPage()

	notebook.AppendPageMenu(screenPage, gtk.NewLabel("screen"), gtk.NewLabel("hey"))
	notebook.AppendPageMenu(windowPage, gtk.NewLabel("window"), gtk.NewLabel("hey2"))
	notebook.AppendPageMenu(regionPage, gtk.NewLabel("region"), gtk.NewLabel("hey2"))

	return notebook
}
