/* Host-side Unity output implementation. */
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

void unityOutputStart(unsigned long baud) { (void)baud; }
void unityOutputChar(unsigned int c) { putchar((int)c); }
void unityOutputFlush(void) {}
void unityOutputComplete(void) {}

#ifdef __cplusplus
}
#endif
