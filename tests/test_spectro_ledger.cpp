#include "camera_iq/spectro_ledger.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using camera_iq::read_spectro_ledger;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
  if (condition) {
    std::cout << "[ ok ] " << what << "\n";
    return;
  }
  std::cout << "[fail] " << what << "\n";
  ++failures;
}

constexpr const char* kHeader =
    "group_id,repeat_index,canonical_path,sha256,alias_paths\n";

// Distinct 64-hex digests without embedding six literal lines of hash.
std::string digest(char fill) { return std::string(64, fill); }

bool refuses(const std::string& csv) {
  try {
    (void)read_spectro_ledger(csv);
  } catch (const std::runtime_error&) {
    return true;
  }
  return false;
}

}  // namespace

int main() {
  {
    const std::string csv =
        std::string(kHeader) +
        "scene_01,1,PRD measurments/PRD_01.mat," + digest('a') +
        ",PRD measurments copy/PRD1scene1.mat\n"
        "scene_01,2,PRD measurments/PRD_02.mat," + digest('b') +
        ",PRD measurments copy/PRD2scene1.mat\n"
        "scene_02,1,PRD measurments/PRD_03.mat," + digest('c') + ",\n";
    const auto groups = read_spectro_ledger(csv);
    check(groups.size() == 2, "ledger: rows collapse into their repeat groups");
    // Every assertion below indexes into the parse. Reporting the shape as one
    // failure beats reading them off a vector that is the wrong size.
    if (groups.size() != 2 || groups[0].readings.size() != 2 ||
        groups[1].readings.size() != 1) {
      check(false, "ledger: parse has the shape the assertions below index");
      std::cout << failures << " check(s) failed\n";
      return 1;
    }
    check(groups[0].group_id == "scene_01" && groups[1].group_id == "scene_02",
          "ledger: groups keep the order the ledger declares");
    check(groups[0].readings.size() == 2 && groups[1].readings.size() == 1,
          "ledger: a group carries every one of its canonical readings");
    check(groups[0].readings[1].canonical_path == "PRD measurments/PRD_02.mat",
          "ledger: readings are ordered by repeat index, not by file name");
    check(groups[0].readings[0].alias_paths.size() == 1 &&
              groups[0].readings[0].alias_paths[0] ==
                  "PRD measurments copy/PRD1scene1.mat",
          "ledger: the alias carrying the scene label is retained");
    check(groups[1].readings[0].alias_paths.empty(),
          "ledger: an empty alias field is no alias, not one empty name");
    check(groups[0].readings[0].sha256 == digest('a'),
          "ledger: the declared content digest is retained");
  }

  // A singleton is a real outcome here: three of the 24 scenes were captured
  // once. It must survive parsing rather than be treated as a malformed pair.
  {
    const std::string csv = std::string(kHeader) + "scene_22,1,PRD measurments/PRD_43.mat," +
                            digest('d') + ",\n";
    const auto groups = read_spectro_ledger(csv);
    check(groups.size() == 1 && groups[0].readings.size() == 1,
          "ledger: a group captured once parses as a singleton");
  }

  check(refuses("group_id,repeat_index,canonical_path,sha256\n"),
        "ledger: a header that is not the committed schema is refused");

  check(refuses(std::string(kHeader) + "scene_01,1,a.mat," + digest('a') + "\n"),
        "ledger: a row without all five fields is refused");

  // A gap means a reading the ledger knows about is absent from the group, and
  // averaging what remains would silently report a smaller sample as the whole.
  check(refuses(std::string(kHeader) + "scene_01,1,a.mat," + digest('a') +
                ",\nscene_01,3,b.mat," + digest('b') + ",\n"),
        "ledger: a gap in a group's repeat indices is refused");

  check(refuses(std::string(kHeader) + "scene_01,2,a.mat," + digest('a') +
                ",\nscene_01,1,b.mat," + digest('b') + ",\n"),
        "ledger: repeat indices out of order are refused");

  check(refuses(std::string(kHeader) + "scene_01,1,a.mat," + digest('a') +
                ",\nscene_02,1,b.mat," + digest('b') +
                ",\nscene_01,2,c.mat," + digest('c') + ",\n"),
        "ledger: a group resumed after another group is refused");

  check(refuses(std::string(kHeader) + "scene_01,0,a.mat," + digest('a') + ",\n"),
        "ledger: a repeat index below one is refused");

  check(refuses(std::string(kHeader) + "scene_01,1,a.mat," + digest('a') +
                ",\nscene_02,1,a.mat," + digest('b') + ",\n"),
        "ledger: the same canonical path in two groups is refused");

  // Two rows with one digest are one reading counted twice, which would inflate
  // a group's sample size without adding a capture.
  check(refuses(std::string(kHeader) + "scene_01,1,a.mat," + digest('a') +
                ",\nscene_02,1,b.mat," + digest('a') + ",\n"),
        "ledger: the same content digest under two paths is refused");

  check(refuses(std::string(kHeader) + "scene_01,1,a.mat," + digest('a') +
                ",alias.mat\nscene_02,1,b.mat," + digest('b') +
                ",alias.mat\n"),
        "ledger: an alias claimed by two readings is refused");

  check(refuses(std::string(kHeader) + "scene_01,1,a.mat," + digest('a') +
                ",b.mat\nscene_02,1,b.mat," + digest('b') + ",\n"),
        "ledger: an alias that is also a canonical path is refused");

  check(refuses(std::string(kHeader) + "scene_01,1,/etc/passwd," + digest('a') +
                ",\n"),
        "ledger: an absolute canonical path is refused");

  check(refuses(std::string(kHeader) + "scene_01,1,../outside.mat," +
                digest('a') + ",\n"),
        "ledger: a canonical path escaping the archive root is refused");

  check(refuses(std::string(kHeader) + "scene_01,1,a.mat," + digest('a') +
                ",../outside.mat\n"),
        "ledger: an alias escaping the archive root is refused");

  check(refuses(std::string(kHeader) + "scene_01,1,," + digest('a') + ",\n"),
        "ledger: an empty canonical path is refused");

  check(refuses(std::string(kHeader) + "scene_01,1,a.mat," +
                std::string(64, 'A') + ",\n"),
        "ledger: an uppercase content digest is refused");

  check(refuses(std::string(kHeader) + "scene_01,1,a.mat," +
                std::string(63, 'a') + ",\n"),
        "ledger: a digest that is not 64 characters is refused");

  check(refuses(std::string(kHeader) + ",1,a.mat," + digest('a') + ",\n"),
        "ledger: an empty group name is refused");

  check(refuses(std::string(kHeader) + "scene_01,one,a.mat," + digest('a') +
                ",\n"),
        "ledger: a repeat index that is not a number is refused");

  check(refuses(kHeader), "ledger: a ledger with no readings is refused");

  if (failures != 0) {
    std::cout << failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "all tests passed\n";
  return 0;
}
