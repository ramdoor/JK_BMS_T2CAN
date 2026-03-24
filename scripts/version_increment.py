#!/usr/bin/env python3
"""
Version increment script for PlatformIO
Automatically increments build number and updates version header
"""

Import("env")
import datetime
import os

def get_version_info():
    """Read or create version file"""
    version_file = "include/version.h"
    
    if os.path.exists(version_file):
        with open(version_file, 'r') as f:
            content = f.read()
            # Extract current build number
            for line in content.split('\n'):
                if 'BUILD_NUMBER' in line:
                    try:
                        build_num = int(line.split()[-1])
                        return build_num + 1
                    except:
                        pass
    
    return 1  # Start from 1 if file doesn't exist

def update_version_header(build_number):
    """Update version.h with new build info"""
    version_file = "include/version.h"
    
    content = f"""// Auto-generated version file
// Do not edit manually - managed by build system

#ifndef VERSION_H
#define VERSION_H

#define FW_VERSION_MAJOR 1
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 0
#define BUILD_NUMBER {build_number}
#define BUILD_TIMESTAMP "{datetime.datetime.now().isoformat()}"
#define GIT_COMMIT "unknown"  // TODO: Extract from git

#define FW_VERSION_STRING "v1.0.0-build{build_number}"

#endif // VERSION_H
"""
    
    os.makedirs("include", exist_ok=True)
    with open(version_file, 'w') as f:
        f.write(content)
    
    print(f"✅ Version updated: Build #{build_number}")

# Run version increment
build_num = get_version_info()
update_version_header(build_num)

# Add build flags with version info
env.Append(CPPDEFINES=[
    ("BUILD_NUMBER", build_num),
    ("FW_VERSION", '\\"v1.0.0\\"')
])
