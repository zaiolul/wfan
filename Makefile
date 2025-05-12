
export EXE_SCANNER=wfan_scan
export EXE_MANAGER=main.py
SCANNER_SRC=./scanning
MANAGER_SRC=./management
INCLUDE_DIR=$(SCANNER_SRC)/include

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

clean:
	rm -f $(SCANNER_SRC)/*.o
	rm -f $(SCANNER_SRC)/json/*.o

.PHONY : clean all