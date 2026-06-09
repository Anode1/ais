# Top-level convenience. The C engine lives in c/; delegate the common targets
# so `make`, `make ut`, and `make clean` work from the repository root too.
.PHONY: all ut clean
all ut clean:
	$(MAKE) -C c $@
