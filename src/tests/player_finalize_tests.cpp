#include "../db.h"
#include <gtest/gtest.h>

#include <dirent.h>
#include <stdio.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

void write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    ASSERT_NE(f, nullptr);
    fputs(content, f);
    fclose(f);
}

std::string read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return std::string("<<missing:") + path + ">>";
    }
    std::string out;
    char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        out.append(buf, n);
    }
    fclose(f);
    return out;
}

int count_files(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) {
        return -1;
    }
    int count = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] != '.') {
            count++;
        }
    }
    closedir(d);
    return count;
}

} // namespace

// Both finalizers must (1) yield byte-identical output and (2) leave exactly one
// file in their directory (the stale, differently-suffixed seed must be removed).
TEST(PlayerFinalize, ByteIdenticalAndSingleFile) {
    const char *legacy_dir = "pf_test_legacy";
    const char *new_dir = "pf_test_new";
    mkdir(legacy_dir, 0775);
    mkdir(new_dir, 0775);

    // Stale versioned files (suffix ".stale", same "probe." prefix) that the glob must delete.
    write_file("pf_test_legacy/probe.stale", "OLD");
    write_file("pf_test_new/probe.stale", "OLD");

    // Each finalizer gets its own scratch (rename consumes its source); scratches live
    // OUTSIDE the target dirs, mirroring production's players/temp vs the bucket dir.
    write_file("pf_test_legacy_scratch", "PLAYER-BYTES-V1\n");
    write_file("pf_test_new_scratch", "PLAYER-BYTES-V1\n");

    bool ok_legacy = finalize_player_file_legacy("pf_test_legacy_scratch", "pf_test_legacy/probe",
                                                 "pf_test_legacy/probe.50.1.123.0.0");
    bool ok_new = finalize_player_file_rename("pf_test_new_scratch", "pf_test_new", "probe",
                                              "pf_test_new/probe.50.1.123.0.0");

    EXPECT_TRUE(ok_legacy);
    EXPECT_TRUE(ok_new);

    EXPECT_EQ(read_file("pf_test_legacy/probe.50.1.123.0.0"),
              read_file("pf_test_new/probe.50.1.123.0.0"));

    EXPECT_EQ(count_files(legacy_dir), 1);
    EXPECT_EQ(count_files(new_dir), 1);

    // Cleanup.
    unlink("pf_test_legacy/probe.50.1.123.0.0");
    unlink("pf_test_new/probe.50.1.123.0.0");
    unlink("pf_test_legacy_scratch"); // cp left this; rename already consumed the new scratch.
    rmdir(legacy_dir);
    rmdir(new_dir);
}
