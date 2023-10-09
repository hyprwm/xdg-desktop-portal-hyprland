package window

import (
	"fmt"
	"github.com/edjubert/hyprland-ipc-go/hyprctl"
)

func getWindowList() []Window {
	getter := hyprctl.Get{}
	clients, err := getter.Clients()
	if err != nil {
		fmt.Println(err)
	}
	var windows []Window
	for _, client := range clients {
		windows = append(windows, Window{
			Title:   client.Title,
			Class:   client.Class,
			Address: client.Address,
			X:       client.At[0],
			Y:       client.At[1],
			Width:   client.Size[0],
			Height:  client.Size[1],
		})
	}

	return windows
}
