# poc-rust-merge -- the ais k-way merge reimplemented in Rust, linked into a C
# harness and checked against a C reference. Shows the FFI drop-in, the (zero)
# dependencies, and the binary-size delta vs pure C. See README.md.
#
#   make          build both binaries + the Rust archive
#   make check    run both against a real index (set IDX=/path/to/index)
#   make size     print the byte sizes and the Rust delta
#   make clean

RUSTC ?= rustc
CC    ?= cc
IDX   ?= /tmp/ais_1m

all: poc poc_c

# Rust staticlib: no_std, no external crates, no cargo -- rustc emits a .a you
# link exactly like a C archive.
libaismerge.a: merge.rs
	$(RUSTC) --edition 2021 -O -C panic=abort -C strip=symbols \
	         --crate-type staticlib merge.rs -o libaismerge.a

# gc-sections drops the unreferenced core code the Rust .a drags in; strip
# removes symbols. Together they take the Rust delta from ~2.4 MB to ~2 KB.
LEAN = -ffunction-sections -fdata-sections -Wl,--gc-sections

poc: poc.c libaismerge.a          # C harness + Rust merge
	$(CC) -O2 $(LEAN) -o poc poc.c libaismerge.a
	strip -s poc

poc_c: poc.c                      # same harness, C merge only (size baseline)
	$(CC) -O2 $(LEAN) -DBASELINE -o poc_c poc.c
	strip -s poc_c

size: poc poc_c libaismerge.a
	@echo "=== bytes ==="; stat -c '%9s  %n' libaismerge.a poc_c poc
	@echo "=== Rust delta (poc - poc_c) ==="; \
	 echo "$$(( $$(stat -c%s poc) - $$(stat -c%s poc_c) )) bytes"

check: poc poc_c
	@k1=$$(find "$(IDX)/idx" -type f -printf '%s %p\n' | sort -rn | sed -n 1p | cut -d' ' -f2-); \
	 k2=$$(find "$(IDX)/idx" -type f -printf '%s %p\n' | sort -rn | sed -n 2p | cut -d' ' -f2-); \
	 echo "keys: $$(basename $$k1)  $$(basename $$k2)"; \
	 echo -n "rust : "; ./poc   "$$k1" "$$k2"; \
	 echo -n "c    : "; ./poc_c "$$k1" "$$k2"

clean:
	rm -f poc poc_c libaismerge.a *.o
