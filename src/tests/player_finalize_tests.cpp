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

// Both finalizers must: (1) write byte-identical output (pinned to the known literal),
// (2) delete every stale "<base>." file via the dot-anchored glob while leaving a
// different player's "<base>name.*" file untouched, and (3) honor the move-vs-copy
// distinction (rename consumes its scratch; cp leaves it).
TEST(PlayerFinalize, ByteIdenticalAndSingleFile) {
    const char *legacy_dir = "pf_test_legacy";
    const char *new_dir = "pf_test_new";
    mkdir(legacy_dir, 0775);
    mkdir(new_dir, 0775);

    // Two stale "probe." versioned files (different suffixes) the glob MUST delete...
    write_file("pf_test_legacy/probe.stale", "OLD");
    write_file("pf_test_new/probe.stale", "OLD");
    write_file("pf_test_legacy/probe.42.1.99.0.0", "OLD2");
    write_file("pf_test_new/probe.42.1.99.0.0", "OLD2");
    // ...and a DIFFERENT player's file that starts with "probe" but NOT "probe." -- the
    // dot anchor must leave it untouched (the bob. vs bobby. regression guard).
    write_file("pf_test_legacy/probename.7.1.124.0.0", "KEEP");
    write_file("pf_test_new/probename.7.1.124.0.0", "KEEP");

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

    // (1) Output is the exact scratch content, and the two finalizers agree.
    EXPECT_EQ(read_file("pf_test_new/probe.50.1.123.0.0"), "PLAYER-BYTES-V1\n");
    EXPECT_EQ(read_file("pf_test_legacy/probe.50.1.123.0.0"),
              read_file("pf_test_new/probe.50.1.123.0.0"));

    // (2) Both stale "probe." files are gone; the "probename" decoy survives (dot anchor).
    EXPECT_NE(access("pf_test_legacy/probe.stale", F_OK), 0);
    EXPECT_NE(access("pf_test_new/probe.stale", F_OK), 0);
    EXPECT_NE(access("pf_test_legacy/probe.42.1.99.0.0", F_OK), 0);
    EXPECT_NE(access("pf_test_new/probe.42.1.99.0.0", F_OK), 0);
    EXPECT_EQ(access("pf_test_legacy/probename.7.1.124.0.0", F_OK), 0);
    EXPECT_EQ(access("pf_test_new/probename.7.1.124.0.0", F_OK), 0);
    // Each dir now holds exactly the new versioned file + the surviving decoy.
    EXPECT_EQ(count_files(legacy_dir), 2);
    EXPECT_EQ(count_files(new_dir), 2);

    // (3) rename consumed the new scratch; cp left the legacy scratch in place.
    EXPECT_NE(access("pf_test_new_scratch", F_OK), 0);
    EXPECT_EQ(access("pf_test_legacy_scratch", F_OK), 0);

    // Cleanup.
    unlink("pf_test_legacy/probe.50.1.123.0.0");
    unlink("pf_test_new/probe.50.1.123.0.0");
    unlink("pf_test_legacy/probename.7.1.124.0.0");
    unlink("pf_test_new/probename.7.1.124.0.0");
    unlink("pf_test_legacy_scratch");
    rmdir(legacy_dir);
    rmdir(new_dir);
}
