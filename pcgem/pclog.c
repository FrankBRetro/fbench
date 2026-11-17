/* pclog.c - the PC GEM layer (pgaes.c, pgvdi.c) can log through edlog();
 * Fbench has its own debug log, so this goes nowhere */
void edlog(const char *fmt, ...) { (void)fmt; }
