package screen

import (
	"fmt"
	"github.com/edjubert/hyprland-ipc-go/hyprctl"
)

func getScreenList() []Screen {
	getter := hyprctl.Get{}
	monitors, err := getter.Monitors("-j")
	if err != nil {
		fmt.Println(err)
	}
	var screens []Screen
	for _, monitor := range monitors {
		screens = append(screens, Screen{
			Label:  monitor.Name,
			Index:  monitor.Id,
			X:      monitor.X,
			Y:      monitor.Y,
			Width:  monitor.Width,
			Height: monitor.Height,
		})
	}

	return screens
}
