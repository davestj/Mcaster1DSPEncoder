// config_yaml.h — YAML-based config read/write for Mcaster1DSPEncoder
//
// Drop-in replacements for readConfigFile() / writeConfigFile() that use
// libyaml (vcpkg libyaml:x86-windows) instead of the legacy INI format.
//
// File naming mirrors the INI system:
//   {g->gConfigFileName}_{g->encoderNumber}.yaml
//
// On first run, readConfigYAML() returns 0 (file not found).  The caller
// (mcaster1_init) then calls the legacy readConfigFile() and afterwards
// writeConfigYAML() to create the YAML file — automatic one-time migration.

#pragma once

#if !defined(AFX_CONFIG_YAML_H__INCLUDED_)
#define AFX_CONFIG_YAML_H__INCLUDED_

#include "libmcaster1dspencoder.h"

#define YAML_CONFIG_VERSION "1.0"

#ifdef __cplusplus
extern "C" {
#endif

// Read encoder config from {g->gConfigFileName}_{g->encoderNumber}.yaml.
// Populates mcaster1Globals and fires bitrateCallback / streamTypeCallback
// exactly as readConfigFile() does.
// Returns 1 on success, 0 if the YAML file does not exist.
int readConfigYAML(mcaster1Globals *g);

// Write encoder config to {g->gConfigFileName}_{g->encoderNumber}.yaml.
// Writes all standard INI-equivalent keys PLUS extended Windows codec fields
// (OpusComplexity, FdkAacProfile, LameVBRMode2, LameVBRQuality, LameABRMean).
// Returns 1 on success, 0 on error.
int writeConfigYAML(mcaster1Globals *g);

#ifdef __cplusplus
}
#endif

#endif /* AFX_CONFIG_YAML_H__INCLUDED_ */
