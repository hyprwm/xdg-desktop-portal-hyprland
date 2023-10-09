package region

import (
	"fmt"
	"github.com/edjubert/hyprland-ipc-go/hyprctl"
	"os"
	"os/exec"
	"regexp"
	"strconv"
)

func createDrawingRegion() Region {
	cmd := exec.Command("slurp")
	out, err := cmd.Output()
	if err != nil {
		fmt.Println("could not run slurp ", err)
	}

	findDigit := regexp.MustCompile(`(\d.*),(\d.*) (\d.*)x(\d.*)`)
	match := findDigit.FindSubmatch(out)
	if len(match) != 5 {
		fmt.Println("len: ", len(match))
		os.Exit(1)
	}

	x, _ := strconv.Atoi(string(match[1]))
	y, _ := strconv.Atoi(string(match[2]))
	width, _ := strconv.Atoi(string(match[3]))
	height, _ := strconv.Atoi(string(match[4]))

	getter := hyprctl.Get{}
	monitors, err := getter.Monitors("-j")
	if err != nil {
		fmt.Println(err)
	}

	region := Region{X: x, Y: y, Width: width, Height: height}
	for _, monitor := range monitors {
		if x > monitor.X && x < monitor.X+monitor.Width && y > monitor.Y && y < monitor.Y+monitor.Height {
			region.Screen = monitor.Name
		}
	}
	fmt.Println(region)

	return region
}
