/* Cable: CMake Bootstrap Library.
 * Copyright 2018 Pawel Bylica.
 * Licensed under the Apache License, Version 2.0. See the LICENSE file.
 */

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

struct buildinfo
{
    const char* project_name;
    const char* project_version;
    const char* project_name_with_version;
    const char* git_commit_hash;
    const char* system_name;
    const char* system_processor;
    const char* compiler_id;
    const char* compiler_version;
    const char* build_type;
};

const struct buildinfo* aleth_get_buildinfo();

#ifdef __cplusplus
}
#endif
