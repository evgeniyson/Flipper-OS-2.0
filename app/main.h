#pragma once
/* Entry points of the portable firmware. The platform initialises its
 * hardware, calls fos_start() once and then fos_step() forever. */
void fos_start(void);
void fos_step(void);
