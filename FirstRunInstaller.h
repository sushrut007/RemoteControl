#pragma once

class FirstRunInstaller
{
public:
    // Returns false when the current process should exit (install/redirect handled).
    static bool ensureInstalled(int argc, char* argv[]);
};
