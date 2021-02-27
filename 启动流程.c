////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//内核启动第一阶段（ASM部分）
////////////////////////////////////////////////////////////////////////////////////////////////////////////////
head.S
------------------------------------
A. ARMv4上的：
------------------------------------
        1. kernel运行的史前时期和内存布局
        在arm平台下，zImage.bin压缩镜像是由bootloader加载到物理内存，然后跳到zImage.bin里一段程序，它专门于将被压缩的kernel解压缩到KERNEL_RAM_PADDR开始的一段内存中，
        接着跳进真正的kernel去执行。
        该kernel的执行起点是stext函数，定义于arch/arm/kernel/head.S。
        
        先看此时内存的布局: 
        以下全属于ARM CPU地址空间，其中 0x30000000~0x32000000 是SDRAM空间。
         ___________
        |           |
        |           |
        |           |
        |           |
        |           |
        |___________|0x32000000(PHYS+SDRAM_SIZE，64M)
        |           |
        |           |
        |           |
        |           |
        |___________|
        |           |
        |  vmlinux  |
        |   image   |
        |___________|0x30008000(KERNEL_RAM_PADDR，解压后的kernel)
        |           |
        |           |
        |___________|0x30000000(PHYS_OFFSET，SDRAM的开始地址)
        |           |
        |           |
        |           |
        |           |
        |           |
        |___________|
        
        
         
        在开发板tqs3c2440中，SDRAM连接到内存控制器的Bank6中，所以它的开始内存地址是0x30000000，大小为64M，即0x2000000。 
        ARM Linux kernel将SDRAM的开始地址定义为PHYS_OFFSET。
        经bootloader加载的kernel,由自解压部分代码运行后，最终kernel被放置到KERNEL_RAM_PADDR（=PHYS_OFFSET + TEXT_OFFSET，即0x30008000）地址上的一段内存，经此放置后，kernel代码以后均不会被移动。
        
        在进入kernel代码前，即bootloader和自解压缩阶段，ARM未开启MMU功能。因此kernel启动代码一个重要功能是设置好相应的页表，并开启MMU功能。
        
        为了支持MMU功能，kernel镜像中的所有符号，包括代码段和数据段的符号，在链接时都生成了:在开启MMU时，所在物理内存地址->映射到的虚拟内存地址。
        
        以arm kernel第一个符号（函数）stext为例，在编译链接，它生成的虚拟地址是0xc0008000，而放置它的物理地址为0x30008000（还记得这是PHYS_OFFSET+TEXT_OFFSET？）。
        
        实际上这个变换可以利用简单的公式进行表示：va = pa – PHYS_OFFSET + PAGE_OFFSET。
        
        Arm linux最终的kernel空间的页表，就是按照这个关系来建立。
        
        之所以较早提及arm linux 的内存映射，原因是在进入kernel代码，里面所有符号地址值为清一色的0xCXXXXXXX地址，而此时ARM未开启MMU功能，故在执行stext函数第一条执行时，它的PC值就是stext所在的内存地址（即物理地址，0x30008000）。
        
        因此，下面有些代码，需要使用地址无关技术！   (地址无关码，难点。。。。)
        
        2. 一览stext函数
        这里的启动流程指的是解压后kernel开始执行的一部分代码，这部分代码和ARM体系结构是紧密联系在一起的，所以最好是将ARM ARCHITECTURE REFERENCE MANUL仔细读读，尤其里面关于控制寄存器啊，MMU方面的内容～
        stext函数定义在Arch/arm/kernel/head.S，它的功能是获取处理器类型和机器类型信息，并创建临时的页表，然后开启MMU功能，并跳进第一个C语言函数start_kernel。
        stext函数的在前置条件是：MMU, D-cache, 关闭; r0 = 0, r1 = machine nr, r2 = atags prointer.
              前面说过解压以后，代码会跳到解压完成以后的vmlinux开始执行，具体从什么地方开始执行我们可以看看生成的vmlinux.lds(arch/arm/kernel/)这个文件：

        1. OUTPUT_ARCH(arm)    
        2. ENTRY(stext)    
        3. jiffies = jiffies_64;    
        4. SECTIONS    
        5. {    
        6.  . = 0x80000000 + 0x00008000;    
        7.  .text.head : {     
        8.   _stext = .;    
        9.   _sinittext = .;    
        0.   *(.text.h    

        很明显我们的vmlinx最开头的section是.text.head，这里我们不能看ENTRY的内容，以为这时候我们没有操作系统，根本不知道如何来解析这里的入口地址，
        我们只能来分析他的section(不过一般来说这里的ENTRY和我们从seciton分析的结果是一样的)，这里的.text.head section我们很容易就能在arch/arm/kernel/head.S里面找到，
        而且它里面的第一个符号就是我们的stext：

        # .section ".text.head", "ax"    
        #     
        # ENTRY(stext)   
        #     
        #  /* 设置CPU运行模式为SVC，并关中断 */    
        #     
        #   msr  cpsr_c, #PSR_F_BIT | PSR_I_BIT | SVC_MODE @ ensure svc mode    
        #     
        #                                      @ and irqs disabled    
        #     
        #   mrc p15, 0, r9, c0, c0        @ get processor id    
        #     
        #   bl    __lookup_processor_type         @ r5=procinfo r9=cupid    
        #     
        # /* r10指向cpu对应的proc_info记录 */    
        #     
        #    movs  r10, r5                         @ invalid processor (r5=0)?    
        #     
        #   beq __error_p                    @ yes, error 'p'    
        #     
        #   bl    __lookup_machine_type            @ r5=machinfo    
        #     
        # /* r8 指向开发板对应的arch_info记录 */    
        #     
        #    movs  r8, r5                           @ invalid machine (r5=0)?    
        #     
        #   beq __error_a                    @ yes, error 'a'    
        #     
        # /* __vet_atags函数涉及bootloader造知kernel物理内存的情况，我们暂时不分析它。 */    
        #     
        #   bl    __vet_atags    
        #     
        # /*  创建临时页表 */    
        #     
        #   bl    __create_page_tables    
            
        #   /*   
        #    
        #    * The following calls CPU specific code in a position independent   
        #    
        #    * manner.  See arch/arm/mm/proc-*.S for details.  r10 = base of   
        #    
        #    * xxx_proc_info structure selected by __lookup_machine_type   
        #    
        #    * above.  On return, the CPU will be ready for the MMU to be   
        #    
        #    * turned on, and r0 will hold the CPU control register value.   
        #    
        #    */    
        #     
        #  /* 这里的逻辑关系相当复杂，先是从proc_info结构中的中跳进__arm920_setup函数，   
        #    
        #   * 然后执__enable_mmu 函数。最后在__enable_mmu函数通过mov pc, r13来执行__switch_data，   
        #    
        #   * __switch_data函数在最后一条语句，鱼跃龙门，跳进第一个C语言函数start_kernel。   
        #    */    
        #     
        #   ldr   r13, __switch_data             @ address to jump to after    
        #     
        #                                      @ mmu has been enabled    
        #     
        #   adr  lr, __enable_mmu        @ return (PIC) address    
        #     
        #   add pc, r10, #PROCINFO_INITFUNC    
        #     
        # ENDPROC(stext)  

         这里的ENTRY这个宏实际我们可以在include/linux/linkage.h里面找到，可以看到他实际上就是声明一个GLOBAL Symbol，后面的ENDPROC和END唯一的区别是前面的声明了一个函数，可以在c里面被调用。

                     1. #ifndef ENTRY    
                     2. #define ENTRY(name) /    
                     3.   .globl name; /    
                     4.   ALIGN; /    
                     5.   name:    
                     6. #endif    
                     7. #ifndef WEAK    
                     8. #define WEAK(name)     /    
                     9.     .weak name;    /    
                    10.     name:    
                    11. #endif    
                    12. #ifndef END    
                    13. #define END(name) /    
                    14.   .size name, .-name    
                    15. #endif    
                    16. /* If symbol 'name' is treated as a subroutine (gets called, and returns)  
                    17.  * then please use ENDPROC to mark 'name' as STT_FUNC for the benefit of  
                    18.  * static analysis tools such as stack depth analyzer.  
                    19.  */    
                    20. #ifndef ENDPROC    
                    21. #define ENDPROC(name) /    
                    22.   .type name, @function; /    
                    23.   END(name)    
                    24. #endif    

        找到了vmlinux的起始代码就进行分析了.
------------------------------------
B. ARMv8上的：
------------------------------------
    ARMv8上的Linux内核的 head.S 主要工作内容：
            1、从el2特权级退回到el1
            2、确认处理器类型
            3、计算内核镜像的起始物理地址及物理地址与虚拟地址之间的偏移
            4、验证设备树的地址是否有效
            5、创建页表，用于启动内核
            6、设置CPU（cpu_setup），用于使能MMU
            7、使能MMU
            8、交换数据段
            9、跳转到start_kernel函数继续运行。
    
head-common.S
    
////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//内核启动第二阶段（C部分）
////////////////////////////////////////////////////////////////////////////////////////////////////////////////
I. 
start_kernel() 
       --> setup_arch()
           --> setup_machine_fdt(__atags_pointer);          //__atags_pointer就是来自R2寄存器,R2是TAG LIST地址，或设备树地址。实现在 kernel/arch/arm/kernel/devtree.c
           --> unflatten_device_tree()                                      //执行完unflatten_device_tree()后，DTS节点信息,被解析出来，存到 of_allnodes 链表中!
                    --> __unflatten_device_tree(initial_boot_params, &of_allnodes, early_init_dt_alloc_memory_arch);
                    --> of_alias_scan(early_init_dt_alloc_memory_arch);     // Get pointer to "/chosen" and "/aliasas" nodes for use everywhere(到处)
           --> .init_machine       //启动流程到了板级文件
                        --> of_platform_populate()          //跟据 of_allnodes 链表中的信息，加载 platform_device。实现在 drivers/of/platform.c，是 OF 的标准函数。
                                                            //如好多板文件，及驱动文件当中用此OF系API来加载 platform_device们。


II. 
问：platform_device 已经注册到系统中了，那么其他设备，例如i2c、spi外设是怎样注册到系统中的？
答：
    在注册i2c总线(非控制器驱动！ xxx_bus_type?)时
          -->会调用到qup_i2c_probe()接口，
                -->该接口用于申请总线资源，和 添加i2c适配器（i2c_adapter）。
                       -->1. i2c_add_adapter
                                 --> i2c_register_adapter
                                      --> i2c_scan_static_board_info
                                          --> i2c_new_device (缺点：必须在 i2c_register_adapter 之前要 i2c_register_board_info 掉，大多搞法是在板文件里注册i2c_board_info的)
                                                   -->此时设备和驱动都已加载，于是drvier里的probe方法将被调用。后面流程就都不一样了。

                        -->2. 在成功添加i2c适配器后，会调用of_i2c_register_devices()接口。
                                -->此接口会解析i2c总线节点的子节点（挂载在该总线上的i2c设备节点），获取i2c外设的地址、中断号等硬件信息。
                                    -->然后调用request_module()加载设备对应的驱动文件，调用i2c_new_device()，生成i2c设备！
                                          -->此时设备和驱动都已加载，于是drvier里面的probe方法将被调用。后面流程就都一样了。

     (跟spi一样了，spi_master被创建时，扫描 __board_list --> new spi_device)
     (在soundcore_open打开/dev/dsp节点函数中,会调用到: request_module("sound-slot-%i", unit>>4) 函数,这表示,让linux的用户空间调用/sbin/modprobe函数,加载名为 sound-slot-0.ko 模块)
 
 
 
 加载流程并不是按找从树根到树叶的方式递归注册，而是只注册根节点下的第一级子节点，第二级及之后的子节点暂不注册！
 
 Linux系统下的设备大多都是挂载在"虚拟平台总线"下的，因此在平台总线被注册后，会根据 of_allnodes 节点的树结构，去寻找该总线的子节点，所有的子节点将被作为设备注册到该总线上。
 
 






----------------------------------------------------------------------------------------------------------
详细解释 setup_machine_fdt(__atags_pointer), unflatten_device_tree() 搞的动作：
----------------------------------------------------------------------------------------------------------
      
      关键点：setup_machine_fdt(__atags_pointer)          
                到这个时候 DTB 还只是加载到内存中的 .dtb 文件而已，这个文件中，不仅包含数据结构，还包含了一些文件头等信息，
                kernel 需要从这些信息中，获取到数据结构相关的信息，然后再生成设备树。
                
                1. 这个函数的调用还有个参数 __atags_pointer，似乎是一个指针，干嘛的？（来自于汇编阶段R2寄存器. R2是TAG LIST地址，或设备树地址）
                2. 
                
                struct machine_desc * __init setup_machine_fdt(unsigned int dt_phys)    //TAG LIST或设备树 物理地址
                {
                    ...
                    devtree = phys_to_virt(dt_phys);
                    ...
                    initial_boot_params = devtree;  //TAG LIST或设备树 虚拟地址
                    ...
                }
                struct boot_param_header 结构体，就是老内核的生成 zimage/uimage等时用到的那个header结构！！
                随后 kernel 把这个指针赋给了全局变量 initial_boot_params。也就是说以后 kernel 会是用这个指针指向的数据去初始化 device tree。


                struct boot_param_header {
                    __be32 magic;               /* magic word OF_DT_HEADER */
                    __be32 totalsize;           /* total size of DT block */
                    __be32 off_dt_struct;       /* offset to structure */
                    __be32 off_dt_strings;      /* offset to strings */
                    __be32 off_mem_rsvmap;      /* offset to memory reserve map */
                    __be32 version;             /* format version */
                    __be32 last_comp_version;   /* last compatible version */
                    /* version 2 fields below */
                    __be32 boot_cpuid_phys; /* Physical CPU id we're booting on */
                    /* version 3 fields below */
                    __be32 dt_strings_size; /* size of the DT strings block */
                    /* version 17 fields below */
                    __be32 dt_struct_size; /* size of the DT structure block */
                };
                看这个结构体，很像之前所说的文件头，有魔数、大小、数据结构偏移量、版本等等，kernel 就应该通过这个结构获取数据，并最终生成设备树。




                int of_platform_populate(struct device_node *root, const struct of_device_id *matches, const struct of_dev_auxdata *lookup, struct device *parent)
                {
                    struct device_node *child;
                    int rc = 0;

                    root = root ? of_node_get(root) : of_find_node_by_path("/");
                    if (!root)
                        return -EINVAL;

                    for_each_child_of_node(root, child) {
                        rc = of_platform_bus_create(child, matches, lookup, parent, true);
                        if (rc)
                            break;
                    }

                    of_node_put(root);
                    return rc;
                }
                
                //此函数的注释写得很明白：“Populate platform_devices from device tree data”。
                //在 of_platform_populate 中如果 root 为 NULL，则将 root 赋值为根节点，这个根节点是用 of_find_node_by_path 取到的。
                //但是这个“device tree data”又是从那里来的？
                
                struct device_node *of_find_node_by_path(const char *path)
                {
                    struct device_node *np = of_allnodes;       //很关键的全局变量：of_allnodes
                    unsigned long flags;                        //struct device_node *of_allnodes;

                    raw_spin_lock_irqsave(&devtree_lock, flags);
                    for (; np; np = np->allnext) {
                        if (np->full_name && (of_node_cmp(np->full_name, path) == 0)
                            && of_node_get(np))
                            break;
                    }
                    raw_spin_unlock_irqrestore(&devtree_lock, flags);
                    return np;
                }
                     
            很关键的全局变量：of_allnodes，定义是在 drivers/of/base.c 里面： struct device_node *of_allnodes;
            它应该就是那个所谓的“device tree data”了。它应该指向了 device tree 的根节点。
            
            
问：那这个全局变量 of_allnodes 又是咋来的？
答：device tree 是由 DTC（Device Tree Compiler）编译成二进制文件 DTB（Ddevice Tree Blob）的，然后在系统上电之后由 bootloader 加载到内存中去，
                这个时候还没有 device tree，而在内存中只有一个所谓的 DTB，这只是一个以某个内存地址开始的一堆原始的 dt 数据，没有树结构。
                kernel 的任务需要把这些数据转换成一个树结构，然后再把这棵树的root节点的地址赋值给 of_allnodes 就行了。
                
                没有这个 device tree 那所有的设备就没办法初始化，所以这个 dt 树的形成一定在 kernel 刚刚启动的时候就完成了。
                既然如此，来看看 kernel 初始化的代码（init/main.c）：

                asmlinkage void __init start_kernel(void)
                {
                    ...
                    setup_arch(&command_line);
                    ...
                }
                这个 setup_arch 就是各个CPU架构自己的setup函数，哪个参与编译就调用哪个，在ARM中应当是 arch/arm/kernel/setup.c 中的 setup_arch。                
                void __init setup_arch(char **cmdline_p)
                {
                    ...
                    mdesc = setup_machine_fdt(__atags_pointer); /* kernel/arch/arm/kernel/devtree.c */
                    ...
                    unflatten_device_tree();
                    ...
                }
                

                


        现在回到 setup_arch：

        /* kernel/drivers/of/fdt.c */
        void __init unflatten_device_tree(void)
        {
            __unflatten_device_tree(initial_boot_params, &of_allnodes, early_init_dt_alloc_memory_arch);
            ...
        }

        看见了吧，of_allnodes 就是在这里赋值的，device tree 也是在这里建立完成的。
        __unflatten_device_tree 函数我们就不去深究了，推测其功能应该就是： 解析数据、申请内存、填充结构等等。

        到此为止，device tree 的初始化就算完成了，在以后的启动过程中，kernel 就会依据这个 dt 来初始化各个设备。



    
----------------------------------------------------------------------------------------------------------         

