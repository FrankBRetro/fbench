/* pclog.c - the PC GEM layer logs through edlog() when EdWord builds it
 * with EDLOG; Fbench has no such log, so it goes nowhere */
void edlog(const char *fmt, ...) { (void)fmt; }
