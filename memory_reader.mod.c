#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

BUILD_SALT;
BUILD_LTO_INFO;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0x2ca90791, "module_layout" },
	{ 0xae2ae519, "param_ops_int" },
	{ 0xe8a33f46, "param_ops_ulong" },
	{ 0xd0da656b, "__stack_chk_fail" },
	{ 0xb3a318fc, "filp_close" },
	{ 0x37a0cba, "kfree" },
	{ 0x88932f9b, "kernel_write" },
	{ 0x3c3ff9fd, "sprintf" },
	{ 0x656e4a6e, "snprintf" },
	{ 0xb8b9f817, "kmalloc_order_trace" },
	{ 0x97651e6c, "vmemmap_base" },
	{ 0x800473f, "__cond_resched" },
	{ 0xdafee24c, "get_user_pages_remote" },
	{ 0x7cd8d75e, "page_offset_base" },
	{ 0xeca4aee1, "pv_ops" },
	{ 0xdad13544, "ptrs_per_p4d" },
	{ 0x8a35b432, "sme_me_mask" },
	{ 0x1d19f77b, "physical_mask" },
	{ 0xd033aa56, "boot_cpu_data" },
	{ 0xa648e561, "__ubsan_handle_shift_out_of_bounds" },
	{ 0x72d79d83, "pgdir_shift" },
	{ 0x2ccbce33, "get_pid_task" },
	{ 0x109525fb, "find_get_pid" },
	{ 0xb1027953, "filp_open" },
	{ 0x92997ed8, "_printk" },
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0x2bd90634, "__put_page" },
	{ 0xcfd05c2c, "put_devmap_managed_page" },
	{ 0x587f22d7, "devmap_managed_key" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "CF37B7D3F353F8595B6BB74");
