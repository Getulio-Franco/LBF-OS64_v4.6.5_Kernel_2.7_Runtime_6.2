#include <stdint.h>
#include <stddef.h>
#include "idt.h"
#include "drivers/video.h"
#include "drivers/shell/shell.h"
#include "drivers/proc.h"
#include "drivers/hw/serial.h" // Integração da Serial
#include "mem/vmm.h"
#include "drivers/kernel_try.h"

extern void kernel_main_return_point(void); 
extern process_t* current_process;
extern void isr_mouse();
//extern SerialPort com1_port; // Instância global da sua porta COM1
#define MAX_SERIAL_PORTS 4
extern SerialPort system_serial_ports[MAX_SERIAL_PORTS];

// Instância global do gerenciador de exceção do Ring 0 try except
k_exception_frame_t g_k_exception_env = {0};

// ISRs (Declaradas no Assembly)
extern void isr0();  extern void isr1();  extern void isr2();  extern void isr3();
extern void isr4();  extern void isr5();  extern void isr6();  extern void isr7();
extern void isr8();  extern void isr9();  extern void isr10(); extern void isr11();
extern void isr12(); extern void isr13(); extern void isr14(); extern void isr15();
extern void isr16(); extern void isr17(); extern void isr18(); extern void isr19();
extern void isr20(); extern void isr21(); extern void isr22(); extern void isr23();
extern void isr24(); extern void isr25(); extern void isr26(); extern void isr27();
extern void isr28(); extern void isr29(); extern void isr30(); extern void isr31();
extern void isr_timer();
extern void isr_keyboard();
extern void isr_serial();     // INCLUSÃO: Porta Serial
extern void syscall_int_handler();

struct idt_entry {
    uint16_t base_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  flags;
    uint16_t base_mid;
    uint32_t base_high;
    uint32_t reserved;
} __attribute__((packed));

struct idtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct idt_entry idt[256];
struct idtr idtr_ptr;

const char *exception_messages[] = {
    "Division By Zero", "Debug", "NMI", "Breakpoint", "Overflow",
    "Bound Range Exceeded", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS",
    "Segment Not Present", "Stack-Segment Fault", "General Protection Fault",
    "Page Fault", "Reserved", "x87 Float", "Alignment Check", "Machine Check",
    "SIMD Float", "Virtualization", "Control Protection", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved", "Hypervisor Err", "VMM Err",
    "Security", "Reserved"
};

void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// --- NOVO: Tratador Limpo da Serial ---
void serial_irq_handler(void) {
    // Processa o caractere. O EOI será enviado pelo Assembly para manter o padrão
  //  driver_serial_handle_interrupt(&com1_port);
  
  driver_serial_handle_interrupt(&system_serial_ports[0]);
}

void exception_handler(registers_t *regs) {
    __asm__ volatile("cli");

    uint64_t user_cr3;
    __asm__ volatile("mov %%cr2, %0" : "=r"(user_cr3)); // Apenas para leitura se necessário

    const char* error_name = (regs->int_no < 32) ? exception_messages[regs->int_no] : "Unknown Exception";

    // 1. TRATAMENTO DE EXPANSÃO DINÂMICA DA PILHA (#PF em Ring 3)
    if (regs->int_no == 14) {
        uint64_t fault_address;
        __asm__ volatile("mov %%cr2, %0" : "=r"(fault_address));

        if ((regs->cs & 0x3) != 0) { 
            if (handle_page_fault(user_cr3, fault_address)) {
                return; 
            }
        }
    }

    // 2. TRATAMENTO DE ERROS NO RING 0 (KERNEL)
    if ((regs->cs & 0x3) == 0) {
        
        // --- RECUPERAÇÃO VIA K_TRY / K_EXCEPT NO RING 0 ---
        if (g_k_exception_env.active) {
            g_k_exception_env.active = 0;
            g_k_exception_env.last_error = regs->int_no;

            // Altera o Frame de Interrupção para desviar o fluxo do Kernel
            regs->rip = g_k_exception_env.rip;
            regs->rsp = g_k_exception_env.rsp;
            regs->rbp = g_k_exception_env.rbp;
            regs->rbx = g_k_exception_env.rbx;
            regs->r12 = g_k_exception_env.r12;
            regs->r13 = g_k_exception_env.r13;
            regs->r14 = g_k_exception_env.r14;
            regs->r15 = g_k_exception_env.r15;
            
            // Força o k_setjmp a retornar o número da exceção (diferente de 0)
            regs->rax = regs->int_no; 

            // Se for Page Fault (#PF), limpa o estado de falha na CPU reescrevendo o CR3
            if (regs->int_no == 14) {
                __asm__ volatile("mov %0, %%cr3" :: "r"((uint64_t)PML4_ADDR) : "memory");
            }

            return; // O iretq fará o salto seguro para dentro do bloco K_EXCEPT!
        }

        // --- KERNEL PANIC FATAL (Acontece APENAS se o erro no Ring 0 NÃO estiver dentro de um K_TRY) ---
        vga_print_string("\n!!! KERNEL PANIC FATAL NO RING 0 (UNHANDLED) !!!\nErro: ", 0, 39);
        vga_print_string((char*)error_name, 0, 39);
        
        if (regs->int_no == 14) {  
            uint64_t cr2;
            __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
            vga_print_string("\nEndereço de Page Fault (CR2): ", 0, 39);
        }

        vga_print_string("\nSistema Paralisado.", 0, 39);
        while(1) { __asm__ volatile("hlt"); }
        
    } else {
        // --- USER APP ERROR (Isolamento de aplicativo em Ring 3) ---
        vga_print_string("\n[IDT] App de Ring 3 travou: ", 0, 39);
        vga_print_string((char*)error_name, 0, 39);

        // Restaura o CR3 do processo para conseguir ler suas estruturas com segurança
        if (user_cr3 != PML4_ADDR) {
            __asm__ volatile("mov %0, %%cr3" :: "r"(user_cr3) : "memory");
        }

        process_t* proc = get_current_process();
        if (proc) {
            vga_print_string(" | Proc: ", 0, 39);
            vga_print_string(proc->name, 0, 39);
        }

        if (regs->int_no == 14) {
            vga_print_string(" | Segmentation Fault (#PF nao tratado)", 0, 39);
        }

        vga_print_string("\n[Kernel] Processo isolado e encerrado com seguranca.\n", 0, 39);

        if (proc && proc->pid > 2) {
            terminate_current_process();
        } else {
            while(1) { __asm__ volatile("cli; hlt"); }
        }
    }
}

void set_idt_gate(int n, uint64_t handler, uint8_t flags) {
    idt[n].base_low  = (uint16_t)(handler & 0xFFFF);
    idt[n].selector  = 0x08; 
    idt[n].ist       = 0;
    idt[n].flags     = flags; 
    idt[n].base_mid  = (uint16_t)((handler >> 16) & 0xFFFF);
    idt[n].base_high = (uint32_t)((handler >> 32) & 0xFFFFFFFF);
    idt[n].reserved  = 0;
}

void init_idt(void) {
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);
    
    // MUDANÇA CRÍTICA AQUI PARA SERIAL FUNCIONAR:
    // Antes era 0xF8 (1111 1000). Agora é 0xE8 (1110 1000) para desmascarar a IRQ4 (Serial)
    outb(0x21, 0xE8); 
    outb(0xA1, 0xEF); 

    uint64_t isr_pointers[32] = {
        (uint64_t)isr0,  (uint64_t)isr1,  (uint64_t)isr2,  (uint64_t)isr3,
        (uint64_t)isr4,  (uint64_t)isr5,  (uint64_t)isr6,  (uint64_t)isr7,
        (uint64_t)isr8,  (uint64_t)isr9,  (uint64_t)isr10, (uint64_t)isr11,
        (uint64_t)isr12, (uint64_t)isr13, (uint64_t)isr14, (uint64_t)isr15,
        (uint64_t)isr16, (uint64_t)isr17, (uint64_t)isr18, (uint64_t)isr19,
        (uint64_t)isr20, (uint64_t)isr21, (uint64_t)isr22, (uint64_t)isr23,
        (uint64_t)isr24, (uint64_t)isr25, (uint64_t)isr26, (uint64_t)isr27,
        (uint64_t)isr28, (uint64_t)isr29, (uint64_t)isr30, (uint64_t)isr31
    };

    for (int i = 0; i < 32; i++) {
        set_idt_gate(i, isr_pointers[i], 0x8E);
    }

    set_idt_gate(32,  (uint64_t)isr_timer,    0x8E);
    set_idt_gate(33,  (uint64_t)isr_keyboard, 0x8E);
    set_idt_gate(36,  (uint64_t)isr_serial,   0x8E); // <-- Porta Serial Injetada com sucesso
    set_idt_gate(44,  (uint64_t)isr_mouse,    0x8E); 

    set_idt_gate(128, (uint64_t)syscall_int_handler, 0xEE);

    idtr_ptr.limit = (uint16_t)(sizeof(struct idt_entry) * 256) - 1;
    idtr_ptr.base = (uint64_t)&idt;

    __asm__ volatile("lidt %0" : : "m"(idtr_ptr));
    __asm__ volatile("sti"); // STI DE VOLTA: Essencial para o seu S.O. não congelar no boot!
}
