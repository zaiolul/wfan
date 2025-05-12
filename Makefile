
EXE_SCANNER=wfan_scanner
EXE_MANAGER=wfan_manager
SCANNER_SRC=./scanning
MANAGER_SRC=./management
INCLUDE_DIR=$(SCANNER_SRC)/include
INSTALL_DIR=/usr/local/bin
SCAN_SCRIPT=start_scanner.sh

MANAGER_DESKTOP_ENTRY=wfan_manager.desktop
MANAGER_ICON=wfan_icon.png
DESKTOP_DIR=/usr/share/applications
ICON_DIR=/usr/share/icons/hicolor/128x128/apps

EXTRA_CFLAGS:=-g
SRCS_SCAN=$(wildcard $(SCANNER_SRC)/*.c)
OBJS_SCAN=$(SRCS_SCAN:$(SCANNER_SRC)/%.c=$(SCANNER_SRC)/%.o)

SRCS_JSON=$(wildcard $(SCANNER_SRC)/json/*.c)
OBJS_JSON=$(SRCS_JSON:$(SCANNER_SRC)/json/%.c=$(SCANNER_SRC)/json/%.o)

LIBS_SCAN=$(LIBS_COM) -lpcap -lnl-3 -lnl-genl-3 -lrt -lmosquitto -lpthread

all: scanner

scanner: $(EXE_SCANNER)

$(EXE_SCANNER): $(OBJS_SCAN) $(OBJS_JSON)
	$(CC) $^ -o $@ $(LIBS_SCAN)
	
$(SCANNER_SRC)/%.o: $(SCANNER_SRC)/%.c

$(SCANNER_SRC)/%.o: $(SCANNER_SRC)/%.c
	$(CC) $(EXTRA_CFLAGS) -I$(INCLUDE_DIR) -I$(SCANNER_SRC)/json $(shell pkg-config --cflags --libs libnl-3.0 libnl-genl-3.0) -c $< -o $@  

install_scanner: $(EXE_SCANNER)
	mkdir -p $(INSTALL_DIR)
	cp $(EXE_SCANNER) $(INSTALL_DIR)
	cp $(SCANNER_SRC)/$(SCAN_SCRIPT) $(INSTALL_DIR)
	chmod +x $(INSTALL_DIR)/$(EXE_SCANNER)

uninstall_scanner:
	rm -f $(INSTALL_DIR)/$(EXE_SCANNER)
	rm -f $(INSTALL_DIR)/$(SCAN_SCRIPT)

install_manager:
	mkdir -p $(INSTALL_DIR)
	ln -sf $(realpath $(MANAGER_SRC)/main.py) $(INSTALL_DIR)/$(EXE_MANAGER)
	chmod +x $(INSTALL_DIR)/$(EXE_MANAGER)
	
	cp $(MANAGER_SRC)/files/$(MANAGER_DESKTOP_ENTRY) $(DESKTOP_DIR)
	cp $(MANAGER_SRC)/files/$(MANAGER_ICON) $(ICON_DIR)
	
uninstall_manager:
	rm -f $(INSTALL_DIR)/$(EXE_MANAGER)
	rm -f $(DESKTOP_DIR)/$(MANAGER_DESKTOP_ENTRY)
	rm -f $(ICON_DIR)/$(MANAGER_ICON)
	
clean:
	rm -f $(SCANNER_SRC)/*.o
	rm -f $(SCANNER_SRC)/json/*.o

.PHONY : clean all install_scanner install_manager uninstall_scanner uninstall_manager