
export EXE_SCANNER=wfan_scan
export EXE_MANAGER=wfan_mgr
SCANNER_SRC=./scanner
MANAGER_SRC=./manager
COMMON_SRC=./common
INCLUDE_DIR=./include

EXTRA_CFLAGS:=-g
SRCS_COM=$(wildcard $(COMMON_SRC)/*.c)
OBJS_COM=$(SRCS_COM:$(COMMON_SRC)/%.c=$(COMMON_SRC)/%.o)

SRCS_SCAN=$(wildcard $(SCANNER_SRC)/*.c)
OBJS_SCAN=$(SRCS_SCAN:$(SCANNER_SRC)/%.c=$(SCANNER_SRC)/%.o)

SRCS_MAN=$(wildcard $(MANAGER_SRC)/*.c)
OBJS_MAN=$(SRCS_MAN:$(MANAGER_SRC)/%.c=$(MANAGER_SRC)/%.o)

SRCS_JSON=$(wildcard $(COMMON_SRC)/json/*.c)
OBJS_JSON=$(SRCS_JSON:$(COMMON_SRC)/json/%.c=$(COMMON_SRC)/json/%.o)
OBJS_COM += $(OBJS_JSON)

LIBS_COM=-lmosquitto -lpthread
LIBS_SCAN=$(LIBS_COM) -lpcap -lnl-3 -lnl-genl-3

all: scanner manager

scanner: $(EXE_SCANNER)

manager: $(EXE_MANAGER)

$(EXE_SCANNER): $(OBJS_COM) $(OBJS_SCAN)
	$(CC) $^ -o $@ $(LIBS_SCAN)

$(EXE_MANAGER): $(OBJS_COM) $(OBJS_MAN)
	$(CC) $^ -o $@ $(LIBS_COM)

$(COMMON_SRC)/%.o: $(COMMON_SRC)/%.c
	$(CC) $(EXTRA_CFLAGS) -I$(INCLUDE_DIR) -I$(COMMON_SRC) -I$(COMMON_SRC)/json -c $< -o $@ 

$(SCANNER_SRC)/%.o: $(SCANNER_SRC)/%.c
	$(CC) $(EXTRA_CFLAGS) -I$(INCLUDE_DIR) -I$(SCANNER_SRC) -I/usr/include/libnl3  -I$(COMMON_SRC)/json -c $< -o $@  

$(MANAGER_SRC)/%.o: $(MANAGER_SRC)/%.c
	$(CC) $(EXTRA_CFLAGS) -I$(INCLUDE_DIR) -I$(MANAGER_SRC) -c $< -o $@  

clean:
	rm -f $(COMMON_SRC)/*.o
	rm -f $(SCANNER_SRC)/*.o
	rm -f $(MANAGER_SRC)/*.o
	rm -f $(MANAGER_SRC)/$(EXE_MANAGER)
	rm -f $(MANAGER_SRC)/$(EXE_SCANNER)

.PHONY : clean all