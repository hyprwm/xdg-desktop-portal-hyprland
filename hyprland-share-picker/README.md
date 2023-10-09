# hyprland-share-picker-gtk
A simple replacement of hyprland-share-picker in GTK

## Gallery
![image](https://github.com/edjubert/hyprland-share-picker-gtk/assets/16240724/47be60bb-e1a6-4100-9d5a-af3291f6dec4)
![image](https://github.com/edjubert/hyprland-share-picker-gtk/assets/16240724/9656ed44-1e10-4ee3-ab04-de08f1d01448)
![image](https://github.com/edjubert/hyprland-share-picker-gtk/assets/16240724/20ff79cc-afb5-4222-ad23-f4beaf35f831)


## Disclaimer
Warning, this is still a work in progress.
You MUST make a backup of the original binary

## Installation
```bash
go build .
sudo mv /usr/bin/hyprland-share-picker ./hyprland-share-picker-bkup
sudo mv ./hyprland-share-picker-gtk /usr/bin/hyprland-share-picker
```

## Make the window floating
Add this line to your hyprland configuration file
```
windowrulev2=float,class:^(hyprland.share.picker)$
```
