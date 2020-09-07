PREFIX := $(PWD)
PATH := $(PWD)/bin:$(PATH)

FLEX_DIR := flex-2.5.35
FLEX_TAR := $(FLEX_DIR).tar.bz2
FLEX_URL := http://prdownloads.sourceforge.net/flex/$(FLEX_TAR)?download

$(PREFIX)/bin/flex: src/$(FLEX_DIR)
	( cd $< && ./configure --prefix=$(PREFIX) && make && make install )

src/$(FLEX_DIR): src/$(FLEX_TAR)
	( cd src && tar xfj $(FLEX_TAR) )
	-( cd src && cat ../$(notdir $(basename $(basename $(FLEX_DIR))))*patch.diff | patch -p0 )

src/$(FLEX_TAR):
	( mkdir -p src && cd src && wget -T 10 -t 0 -c $(FLEX_URL) -O ${FLEX_TAR} )

