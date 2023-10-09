package region

import (
	"fmt"
	"github.com/diamondburned/gotk4/pkg/gtk/v4"
	"github.com/edjubert/hyprland-share-picker-gtk/src/pkg/tools"
	"os"
)

func CreateRegionPage() *gtk.Box {
	callback := func() {
		region := createDrawingRegion()
		fmt.Printf("region:%s@%d,%d,%d,%d", region.Screen, region.X, region.Y, region.Width, region.Height)
		os.Exit(0)
	}
	regionPage := gtk.NewBox(gtk.OrientationVertical, 0)
	regionPage.Append(tools.CreateButton("Select region...", 6, callback))

	return regionPage
}
