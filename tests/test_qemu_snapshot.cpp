/*
 * Copyright (C) Canonical, Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "common.h"
#include "mock_process_factory.h"
#include "mock_snapshot.h"
#include "mock_virtual_machine.h"
#include "path.h"

#include <multipass/process/process.h>
#include <multipass/virtual_machine_description.h>
#include <multipass/vm_specs.h>
#include <src/platform/backends/qemu/qemu_snapshot.h>

#include <QJsonArray>
#include <QJsonObject>

#include <memory>
#include <unordered_map>

namespace mp = multipass;
namespace mpt = multipass::test;
using namespace testing;

namespace
{

struct PublicQemuSnapshot : public mp::QemuSnapshot
{
    // clang-format off
    // (keeping original declaration order)
    using mp::QemuSnapshot::QemuSnapshot;
    using mp::QemuSnapshot::capture_impl;
    using mp::QemuSnapshot::erase_impl;
    using mp::QemuSnapshot::apply_impl;
    // clang-format on
};

struct TestQemuSnapshot : public Test
{
    using ArgsMatcher = Matcher<QStringList>;

    mp::QemuSnapshot quick_snapshot(const std::string& name = "asdf")
    {
        return mp::QemuSnapshot{name, "", nullptr, specs, vm, desc};
    }

    mp::VirtualMachineDescription desc = [] {
        mp::VirtualMachineDescription ret{};
        ret.image.image_path = "raniunotuiroleh";
        return ret;
    }();
    NiceMock<mpt::MockVirtualMachineT<mp::QemuVirtualMachine>> vm{"qemu-vm"};
    ArgsMatcher list_args_matcher = ElementsAre("snapshot", "-l", desc.image.image_path);
    inline static const auto success = mp::ProcessState{0, std::nullopt};
    inline static const auto specs = [] {
        const auto cpus = 3;
        const auto mem_size = mp::MemorySize{"1.23G"};
        const auto disk_space = mp::MemorySize{"3.21M"};
        const auto state = mp::VirtualMachine::State::off;
        const auto mounts =
            std::unordered_map<std::string, mp::VMMount>{{"asdf", {"fdsa", {}, {}, mp::VMMount::MountType::Classic}}};
        const auto metadata = [] {
            auto metadata = QJsonObject{};
            metadata["meta"] = "data";
            return metadata;
        }();

        return mp::VMSpecs{cpus, mem_size, disk_space, "mac", {}, "", state, mounts, false, metadata, {}};
    }();
};

TEST_F(TestQemuSnapshot, initializesBaseProperties)
{
    const auto name = "name";
    const auto comment = "comment";
    const auto parent = std::make_shared<mpt::MockSnapshot>();

    auto desc = mp::VirtualMachineDescription{};
    auto vm = NiceMock<mpt::MockVirtualMachineT<mp::QemuVirtualMachine>>{"qemu-vm"};

    const auto snapshot = mp::QemuSnapshot{name, comment, parent, specs, vm, desc};
    EXPECT_EQ(snapshot.get_name(), name);
    EXPECT_EQ(snapshot.get_comment(), comment);
    EXPECT_EQ(snapshot.get_parent(), parent);
    EXPECT_EQ(snapshot.get_num_cores(), specs.num_cores);
    EXPECT_EQ(snapshot.get_mem_size(), specs.mem_size);
    EXPECT_EQ(snapshot.get_disk_space(), specs.disk_space);
    EXPECT_EQ(snapshot.get_state(), specs.state);
    EXPECT_EQ(snapshot.get_mounts(), specs.mounts);
    EXPECT_EQ(snapshot.get_metadata(), specs.metadata);
}

TEST_F(TestQemuSnapshot, initializesBasePropertiesFromJson)
{
    const auto parent = std::make_shared<mpt::MockSnapshot>();
    EXPECT_CALL(vm, get_snapshot(2)).WillOnce(Return(parent));

    const mp::QemuSnapshot snapshot{mpt::test_data_path_for("test_snapshot.json"), vm, desc};
    EXPECT_EQ(snapshot.get_name(), "snapshot3");
    EXPECT_EQ(snapshot.get_comment(), "A comment");
    EXPECT_EQ(snapshot.get_parent(), parent);
    EXPECT_EQ(snapshot.get_num_cores(), 1);
    EXPECT_EQ(snapshot.get_mem_size(), mp::MemorySize{"1G"});
    EXPECT_EQ(snapshot.get_disk_space(), mp::MemorySize{"5G"});
    EXPECT_EQ(snapshot.get_state(), mp::VirtualMachine::State::off);

    auto mount_matcher1 = Pair(Eq("guybrush"), Field(&mp::VMMount::mount_type, mp::VMMount::MountType::Classic));
    auto mount_matcher2 = Pair(Eq("murray"), Field(&mp::VMMount::mount_type, mp::VMMount::MountType::Native));
    EXPECT_THAT(snapshot.get_mounts(), UnorderedElementsAre(mount_matcher1, mount_matcher2));

    EXPECT_THAT(
        snapshot.get_metadata(),
        ResultOf([](const QJsonObject& metadata) { return metadata["arguments"].toArray(); }, Contains("-qmp")));
}

TEST_F(TestQemuSnapshot, capturesSnapshot)
{
    auto snapshot_index = 3;
    auto snapshot_tag = fmt::format("@s{}", snapshot_index);
    EXPECT_CALL(vm, get_snapshot_count).WillOnce(Return(snapshot_index - 1));

    auto proc_count = 0;

    ArgsMatcher capture_args_matcher{
        ElementsAre("snapshot", "-c", QString::fromStdString(snapshot_tag), desc.image.image_path)};

    auto mock_factory_scope = mpt::MockProcessFactory::Inject();
    mock_factory_scope->register_callback([&](mpt::MockProcess* process) {
        ASSERT_LE(++proc_count, 2);

        EXPECT_EQ(process->program(), "qemu-img");

        const auto& args_matcher = proc_count == 1 ? list_args_matcher : capture_args_matcher;
        EXPECT_THAT(process->arguments(), args_matcher);

        EXPECT_CALL(*process, execute).WillOnce(Return(success));
    });

    quick_snapshot().capture();
    EXPECT_EQ(proc_count, 2);
}

} // namespace
