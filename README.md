# Swordfish
Build software with 3D status progress like Swordfish movie

![idea](images/swordfish_movie.gif)  


## Current develoment status

![current_status](images/current_status.png)  

# Dependencies
- XDG Shell header and code
```
wayland-scanner server-header /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml desktop-server.h
wayland-scanner public-code /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml desktop-server.c

```
- vulkan drivers
- vulkan headers
- vulkan validation layers
- Xlib
- C compiler
- Wayland Server development

# To test
    make
    sudo make install

Then in the project directory for building  

    swordfish "command"

Ex:  
    
    swordfish make -j8  
    swordfish ninja  
