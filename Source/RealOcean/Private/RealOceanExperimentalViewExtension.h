#pragma once

// M19 keeps the development probe/A-B controls, and can also register the RDG caustics extension
// in Shipping when REALOCEAN_RELEASE_CAUSTICS_PATH is compile-time locked to 1 or 2.
// Default path 0 compiles/ships the authoritative full-resolution material caustics path only.

void StartRealOceanExperimentalViewExtension();
void StopRealOceanExperimentalViewExtension();
