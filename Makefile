
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

LIBS_COM=-lmosquitto -lpthread
LIBS_SCAN=$(LIBS_COM) -lpcap

all: scanner manager

scanner: $(EXE_SCANNER)

manager: $(EXE_MANAGER)

$(EXE_SCANNER): $(OBJS_COM) $(OBJS_SCAN)
	$(CC) $^ -o $@ $(LIBS_SCAN)

$(EXE_MANAGER): $(OBJS_COM) $(OBJS_MAN)
	$(CC) $^ -o $@ $(LIBS_COM)

$(COMMON_SRC)/%.o: $(COMMON_SRC)/%.c
	$(CC) $(EXTRA_CFLAGS) -I$(INCLUDE_DIR) -c $< -o $@ 

$(SCANNER_SRC)/%.o: $(SCANNER_SRC)/%.c
	$(CC) $(EXTRA_CFLAGS) -I$(INCLUDE_DIR) -c $< -o $@  

$(MANAGER_SRC)/%.o: $(MANAGER_SRC)/%.c
	$(CC) $(EXTRA_CFLAGS) -I$(INCLUDE_DIR) -c $< -o $@  

clean:
	rm -f $(COMMON_SRC)/*.o
	rm -f $(SCANNER_SRC)/*.o
	rm -f $(MANAGER_SRC)/*.o
	rm -f $(MANAGER_SRC)/$(EXE_MANAGER)
	rm -f $(MANAGER_SRC)/$(EXE_SCANNER)

.PHONY : clean all