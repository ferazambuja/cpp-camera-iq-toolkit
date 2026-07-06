#pragma once

namespace camera_iq {

// Subcommand entry points. argc/argv exclude the program name and the
// subcommand word itself.
int cmd_manifest(int argc, char** argv);
int cmd_raw_stats(int argc, char** argv);
int cmd_demosaic(int argc, char** argv);
int cmd_dark_calibration(int argc, char** argv);
int cmd_exposure_response(int argc, char** argv);
int cmd_oecf_fit(int argc, char** argv);
int cmd_reference_info(int argc, char** argv);
int cmd_ccm_fit(int argc, char** argv);
int cmd_spectral_response(int argc, char** argv);
int cmd_patches(int argc, char** argv);

}  // namespace camera_iq
