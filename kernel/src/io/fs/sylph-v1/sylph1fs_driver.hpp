#pragma once

#include "../vfs.hpp"           // FsDriver / FsMount / vfs::register_driver
#include "sylph1fs.hpp"         // Sylph1FS（将来、mount後に流用予定）
#include "sylph1fs_structs.hpp" // on-disk Superblock 定義

class Sylph1FsDriver : public FsDriver
{
public:
    const char *name() const override { return "Sylph1FS"; }
    bool probe(BlockDevice &device, Console &con) override;
    FsMount *mount(BlockDevice &device, Console &con) override;
};

bool register_sylph1fs_driver();
