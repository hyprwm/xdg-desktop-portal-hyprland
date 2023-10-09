package tools

import "github.com/diamondburned/gotk4/pkg/gtk/v4"

func CreateButton(label string, margin int, callback func()) *gtk.Button {
	newButton := gtk.NewButtonWithLabel(label)
	newButton.SetMarginStart(margin)
	newButton.SetMarginEnd(margin)
	newButton.SetMarginBottom(margin)
	newButton.SetMarginTop(margin)
	newButton.ConnectClicked(callback)
	return newButton
}
