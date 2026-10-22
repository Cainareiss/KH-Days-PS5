/* Keep a data section without making any reference to the registry object. */
int fixture_anchor = 1;
int fixture_entry(void) { return fixture_anchor; }
