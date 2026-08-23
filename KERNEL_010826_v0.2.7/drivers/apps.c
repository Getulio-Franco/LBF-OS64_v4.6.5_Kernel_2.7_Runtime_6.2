#include "apps.h"
#include "drivers/video.h"
#include "include/elf.h"
#include "drivers/proc.h"
#include "drivers/keyboard.h"
#include "drivers/kernel_try.h" // Proteção K_TRY / K_EXCEPT no Kernel
#include <stdint.h>

extern volatile int vga_ring0_enabled;
// Variáveis Globais do Kernel para a ponte Syscall <-> task_d
extern volatile int g_exec_pending_flag;
extern char g_pending_elf_path[128];
extern uint64_t g_last_exec_pid;

void task_a() {
//nunca usar
}

void task_b() {
    while(1) {
        process_cleanup_zombies(); // Ceifador
        for(volatile int i = 0; i < 500000; i++); 
        __asm__ volatile("hlt"); 
    }
}

void task_c() {
    // Aguarda o sistema estabilizar/drivers carregarem
    for(volatile int i = 0; i < 5000000; i++);

    // 1. Cria o processo do Explorer
    uint64_t pid_explorer = create_elf_process("EXPLORER.ELF");

    if (pid_explorer != (uint64_t)-1) {
        __asm__ volatile("cli");
        
        process_t* proc_explorer = find_process_by_pid(pid_explorer);
        
        if (proc_explorer) {
            // Se houver um processo anterior em foreground (Ex: o Shell do terminal), 
            // removemos o foco primeiro para não haver duplicidade
            if (foreground_process) {
                foreground_process->is_foreground = 0;
            }

            // 2. Entrega oficialmente o controle do hardware ao Explorer
            proc_explorer->is_foreground = 1;
            foreground_process = proc_explorer;
            
            // O Explorer está pronto e assumiu o foreground. 
            // Desligamos o motor gráfico do Ring 0 imediatamente!
            vga_ring0_enabled = 0; // deliga o db_swap_buffers(); do kernel.c

            // 3. Limpa o lixo residual do buffer do teclado antes da GUI ler
            while(keyboard_pop_char() != 0);
        }
        
        __asm__ volatile("sti");
    }

    // Loop de ociosidade da task de boot
    while(1) {
        __asm__ volatile("hlt");
    }
}

void task_d() {
    while(1) {
        if (g_exec_pending_flag == 1) {
            K_TRY {
                // Ponto 1: Mostra que pegou o nome e vai invocar o ELF loader
                vga_print_string("\n[TASK_D v9_v2] indo para o create_elf_processo: ", 0, 38);
                vga_print_string(g_pending_elf_path, 30, 38);

                uint64_t pid_filho = create_elf_process(g_pending_elf_path);

                if (pid_filho != 0 && pid_filho != (uint64_t)-1) {
                    __asm__ volatile("cli");
                    
                    process_t* proc_filho = find_process_by_pid(pid_filho);
                    if (proc_filho) {
                        proc_filho->is_foreground = 1;
                        if (foreground_process) {
                            foreground_process->is_foreground = 0;
                        }
                        foreground_process = proc_filho;
                        
                        proc_filho->state = PROCESS_READY;
                        
                        while(keyboard_pop_char() != 0);
                    }
                    
                    g_last_exec_pid = pid_filho;
                    __asm__ volatile("sti");
                    
                    // Ponto 2A: Sucesso na criação do processo
                    vga_print_string("\n[TASK_D v9_v2] Processo executando, indo ao RING3 ", 0, 39);
                } else {
                    g_last_exec_pid = (uint64_t)-1;
                    
                    // Ponto 2B: Falha mapeada no carregamento do ELF
                    vga_print_string("\n[TASK_D v9_v2] ERRO: Falha em create_elf_process!", 0, 39);
                }
            }
            K_EXCEPT {
                // Assegura que interrupções não fiquem desativadas em caso de falha grave
                __asm__ volatile("sti");
                vga_print_string("\n[TASK_D Fault] Excecao capturada durante a execucao da task_d!", 0, 39);
                g_last_exec_pid = (uint64_t)-1;
            }
            K_END_TRY

            g_exec_pending_flag = 0;
        }

        __asm__ volatile("hlt");
    }
}
