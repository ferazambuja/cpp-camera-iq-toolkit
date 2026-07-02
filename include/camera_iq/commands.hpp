#pragma once

namespace camera_iq {

// Subcommand entry points. argc/argv exclude the program name and the
// subcommand word itself.
int cmd_manifest(int argc, char** argv);
int cmd_raw_stats(int argc, char** argv);
int cmd_demosaic(int argc, char** argv);

}  // namespace camera_iq
