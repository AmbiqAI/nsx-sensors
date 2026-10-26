/* SPDX-License-Identifier: BSD-3-Clause */
/* A defined name alone must not masquerade as a linked driver function. */
volatile int bme688_validate_config;
int main(void) { return bme688_validate_config; }
