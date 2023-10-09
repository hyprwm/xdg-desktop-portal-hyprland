package window

import (
	"fmt"
	"github.com/diamondburned/gotk4/pkg/gtk/v4"
	"github.com/edjubert/hyprland-share-picker-gtk/src/pkg/tools"
	"os"
)

func CreateWindowPage() *gtk.Box {
	windowPage := gtk.NewBox(gtk.OrientationVertical, 0)

	for _, window := range getWindowList() {
		callback := func() {
			fmt.Printf("window:%s\n", window.Address)
			os.Exit(0)
		}
		label := fmt.Sprintf("%s: %s", window.Class, window.Title)
		windowPage.Append(tools.CreateButton(label, 6, callback))
	}

	return windowPage
}
