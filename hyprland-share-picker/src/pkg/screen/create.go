package screen

import (
	"fmt"
	"github.com/diamondburned/gotk4/pkg/gtk/v4"
	"github.com/edjubert/hyprland-share-picker-gtk/src/pkg/tools"
	"os"
)

func CreatePage() *gtk.Box {
	screenPage := gtk.NewBox(gtk.OrientationVertical, 0)

	for _, screen := range getScreenList() {
		callback := func() {
			fmt.Printf("screen:%s\n", screen.Label)
			os.Exit(0)
		}
		label := fmt.Sprintf("Screen %d at %d, %d (%dx%d) (%s)", screen.Index, screen.X, screen.Y, screen.Width, screen.Height, screen.Label)
		screenPage.Append(tools.CreateButton(label, 6, callback))
	}

	return screenPage
}
