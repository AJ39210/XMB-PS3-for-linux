CXX = g++
CC = gcc
CXXFLAGS = -std=c++17 -Wall -Icore/services
CFLAGS = -Wall -Icore/services
# Raylib and Linux system link flags
LDFLAGS = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11

TARGET = xmb_desktop
BINARY_NAME = ps3-xmb
INSTALL_DIR = /usr/share/$(BINARY_NAME)

SRCS = core/main.cpp \
       core/services/xml_parser.cpp \
       core/services/tinyxml2.cpp \
       core/services/audio_driver.c

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

install: $(TARGET)
	sudo apt-get update && sudo apt-get install -y openbox maim xclip clipit alsa-utils
	sudo mkdir -p $(INSTALL_DIR)/core/fonts
	sudo cp -r sound Icons background menu.xml $(INSTALL_DIR)/
	sudo cp -r core/fonts $(INSTALL_DIR)/core/
	sudo cp $(TARGET) $(INSTALL_DIR)/$(BINARY_NAME)-bin
	mkdir -p ~/.config/openbox
	echo '<openbox_config>' > ~/.config/openbox/rc.xml
	echo '  <keyboard>' >> ~/.config/openbox/rc.xml
	echo '    <keybind key="A-Tab">' >> ~/.config/openbox/rc.xml
	echo '      <action name="NextWindow">' >> ~/.config/openbox/rc.xml
	echo '        <interactive>yes</interactive>' >> ~/.config/openbox/rc.xml
	echo '      </action>' >> ~/.config/openbox/rc.xml
	echo '    </keybind>' >> ~/.config/openbox/rc.xml
	echo '    <keybind key="A-S-Tab">' >> ~/.config/openbox/rc.xml
	echo '      <action name="PreviousWindow">' >> ~/.config/openbox/rc.xml
	echo '        <interactive>yes</interactive>' >> ~/.config/openbox/rc.xml
	echo '      </action>' >> ~/.config/openbox/rc.xml
	echo '    </keybind>' >> ~/.config/openbox/rc.xml
	echo '    <keybind key="Print">' >> ~/.config/openbox/rc.xml
	echo '      <action name="Execute">' >> ~/.config/openbox/rc.xml
	echo '        <command>bash -c '\''mkdir -p ~/Pictures && file=~/Pictures/screenshot-$$(date +%Y%m%d-%H%M%S).png && maim -s | tee "$$file" | xclip -selection clipboard -t image/png'\''</command>' >> ~/.config/openbox/rc.xml
	echo '      </action>' >> ~/.config/openbox/rc.xml
	echo '    </keybind>' >> ~/.config/openbox/rc.xml
	echo '    <keybind key="XF86AudioRaiseVolume">' >> ~/.config/openbox/rc.xml
	echo '      <action name="Execute">' >> ~/.config/openbox/rc.xml
	echo '        <command>amixer -D pulse sset Master 5%+ unmute || amixer sset Master 5%+ unmute</command>' >> ~/.config/openbox/rc.xml
	echo '      </action>' >> ~/.config/openbox/rc.xml
	echo '    </keybind>' >> ~/.config/openbox/rc.xml
	echo '    <keybind key="XF86AudioLowerVolume">' >> ~/.config/openbox/rc.xml
	echo '      <action name="Execute">' >> ~/.config/openbox/rc.xml
	echo '        <command>amixer -D pulse sset Master 5%- unmute || amixer sset Master 5%- unmute</command>' >> ~/.config/openbox/rc.xml
	echo '      </action>' >> ~/.config/openbox/rc.xml
	echo '    </keybind>' >> ~/.config/openbox/rc.xml
	echo '    <keybind key="XF86AudioMute">' >> ~/.config/openbox/rc.xml
	echo '      <action name="Execute">' >> ~/.config/openbox/rc.xml
	echo '        <command>amixer -D pulse sset Master toggle || amixer sset Master toggle</command>' >> ~/.config/openbox/rc.xml
	echo '      </action>' >> ~/.config/openbox/rc.xml
	echo '    </keybind>' >> ~/.config/openbox/rc.xml
	echo '  </keyboard>' >> ~/.config/openbox/rc.xml
	echo '</openbox_config>' >> ~/.config/openbox/rc.xml
	echo "#!/bin/bash" | sudo tee /usr/local/bin/$(BINARY_NAME) > /dev/null
	echo "pkill openbox" | sudo tee -a /usr/local/bin/$(BINARY_NAME) > /dev/null
	echo "pkill clipit" | sudo tee -a /usr/local/bin/$(BINARY_NAME) > /dev/null
	echo "clipit &" | sudo tee -a /usr/local/bin/$(BINARY_NAME) > /dev/null
	echo "openbox &" | sudo tee -a /usr/local/bin/$(BINARY_NAME) > /dev/null
	echo "sleep 1" | sudo tee -a /usr/local/bin/$(BINARY_NAME) > /dev/null
	echo "cd $(INSTALL_DIR) && ./$(BINARY_NAME)-bin \"\$$@\"" | sudo tee -a /usr/local/bin/$(BINARY_NAME) > /dev/null
	sudo chmod +x /usr/local/bin/$(BINARY_NAME)
	sudo mkdir -p /usr/share/xsessions
	echo "[Desktop Entry]" | sudo tee /usr/share/xsessions/$(BINARY_NAME).desktop > /dev/null
	echo "Name=PS3 XMB Environment" | sudo tee -a /usr/share/xsessions/$(BINARY_NAME).desktop > /dev/null
	echo "Comment=PlayStation 3 XMB Desktop Session Replacement" | sudo tee -a /usr/share/xsessions/$(BINARY_NAME).desktop > /dev/null
	echo "Exec=/usr/local/bin/$(BINARY_NAME) --installed" | sudo tee -a /usr/share/xsessions/$(BINARY_NAME).desktop > /dev/null
	echo "Type=Application" | sudo tee -a /usr/share/xsessions/$(BINARY_NAME).desktop > /dev/null
	mkdir -p ~/.config/autostart
	echo "#!/bin/bash" > ~/.config/$(BINARY_NAME)-launch.sh
	echo "sleep 5" >> ~/.config/$(BINARY_NAME)-launch.sh
	echo "/usr/local/bin/$(BINARY_NAME) --installed" >> ~/.config/$(BINARY_NAME)-launch.sh
	chmod +x ~/.config/$(BINARY_NAME)-launch.sh
	echo "[Desktop Entry]" > ~/.config/autostart/$(BINARY_NAME).desktop
	echo "Type=Application" >> ~/.config/autostart/$(BINARY_NAME).desktop
	echo "Name=PS3 XMB Environment Autostart" >> ~/.config/autostart/$(BINARY_NAME).desktop
	echo "Exec=$$HOME/.config/$(BINARY_NAME)-launch.sh" >> ~/.config/autostart/$(BINARY_NAME).desktop
	echo "Hidden=false" >> ~/.config/autostart/$(BINARY_NAME).desktop
	echo "NoDisplay=false" >> ~/.config/autostart/$(BINARY_NAME).desktop
	echo "X-GNOME-Autostart-enabled=true" >> ~/.config/autostart/$(BINARY_NAME).desktop
	@echo "[Install] Complete!"

uninstall:
	sudo rm -f /usr/local/bin/$(BINARY_NAME)
	sudo rm -rf $(INSTALL_DIR)
	sudo rm -f /usr/share/xsessions/$(BINARY_NAME).desktop
	rm -f ~/.config/autostart/$(BINARY_NAME).desktop
	rm -f ~/.config/$(BINARY_NAME)-launch.sh
	rm -f ~/.config/openbox/rc.xml
	@echo "[Uninstall] All custom configs, folders, and assets completely removed (APT packages left intact)."

clean:
	rm -f $(TARGET)
	@echo "[Clean] Removed local binary."
