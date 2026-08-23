#include <stddef.h>
#include <stdbool.h>
#include "vmm.h"
#include "pmm.h" 
#include "util/string.h" 
#include <stdint.h>
#include "drivers/proc.h"
#include "drivers/video.h" 
#include "drivers/kernel_try.h" // Proteção K_TRY / K_EXCEPT no Ring 0

/**
 * @brief Navega ou cria tabelas de páginas conforme necessário.
 * @note Protegido com K_TRY contra ponteiros nulos ou tabelas corrompidas.
 */
uint64_t* get_next_table(uint64_t* table, uint64_t index) {
    uint64_t* result = 0;

    K_TRY {
        if (table[index] & PAGE_PRESENT) {
            // SEGURANÇA: Se for uma Huge Page, não podemos criar sub-tabelas nela.
            if (table[index] & PAGE_HUGE) {
                result = 0; 
            } else {
                table[index] |= PAGE_USER | PAGE_WRITE;
                return (uint64_t*)(table[index] & 0x000FFFFFFFFFF000ULL);
            }
        } else {
            // Aloca nova página para a tabela via PMM
            void* new_table_phys = pmm_alloc_block(); 
            if (!new_table_phys) {
                vga_print_string("\n[VMM ERROR] Falha ao alocar pagina no PMM para nova tabela!", 0, 39);
                result = 0; 
            } else {
                uint64_t* new_table_ptr = (uint64_t*)new_table_phys;
                
                // Zera a nova tabela
                memset(new_table_ptr, 0, 4096);

                // Define as flags na tabela PAI
                table[index] = ((uint64_t)new_table_phys) | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
                
                result = new_table_ptr;
            }
        }
    }
    K_EXCEPT {
        vga_print_string("\n[VMM Fault] Exceção capturada em get_next_table!", 0, 39);
        result = 0;
    }
    K_END_TRY

    return result;
}

/**
 * @brief Mapeia 2MB de memória virtual para física.
 */
void vmm_map_page_2mb_ext(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
    K_TRY {
        uint64_t pml4_idx = (virt_addr >> 39) & 0x1FF;
        uint64_t pdpt_idx = (virt_addr >> 30) & 0x1FF;
        uint64_t pd_idx   = (virt_addr >> 21) & 0x1FF;

        uint64_t* pml4 = (uint64_t*)PML4_ADDR;

        uint64_t* pdpt = get_next_table(pml4, pml4_idx);
        if (pdpt) {
            uint64_t* pd = get_next_table(pdpt, pdpt_idx);
            if (pd) {
                pd[pd_idx] = (phys_addr & ~0x1FFFFF) | flags | PAGE_HUGE;
                __asm__ volatile("invlpg (%0)" :: "r"(virt_addr) : "memory");
            }
        }
    }
    K_EXCEPT {
        vga_print_string("\n[VMM Fault] Exceção capturada em vmm_map_page_2mb_ext!", 0, 39);
    }
    K_END_TRY
}

/**
 * @brief Mapeia 4KB de memória virtual para física.
 */
void vmm_map_page_4kb(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
    virt_addr &= ~0xFFF; // Alinhamento 4KB
    phys_addr &= ~0xFFF; 

    K_TRY {
        uint64_t pml4_idx = (virt_addr >> 39) & 0x1FF;
        uint64_t pdpt_idx = (virt_addr >> 30) & 0x1FF;
        uint64_t pd_idx   = (virt_addr >> 21) & 0x1FF;
        uint64_t pt_idx   = (virt_addr >> 12) & 0x1FF; 

        uint64_t* pml4 = (uint64_t*)PML4_ADDR;

        uint64_t* pdpt = get_next_table(pml4, pml4_idx);
        if (pdpt) {
            uint64_t* pd = get_next_table(pdpt, pdpt_idx);
            if (pd) {
                uint64_t* pt = get_next_table(pd, pd_idx);
                if (pt) {
                    pt[pt_idx] = phys_addr | flags;
                    __asm__ volatile("invlpg (%0)" :: "r"(virt_addr) : "memory");
                }
            }
        }
    }
    K_EXCEPT {
        vga_print_string("\n[VMM Fault] Exceção capturada em vmm_map_page_4kb!", 0, 39);
    }
    K_END_TRY
}

/**
 * @brief Mapeia uma área contígua inteira.
 */
void vmm_map_area(uint64_t virt_addr, uint64_t phys_addr, size_t size, uint64_t flags) {
    size_t aligned_size = (size + 0xFFF) & ~0xFFFULL;
    
    for (size_t i = 0; i < aligned_size; i += 4096) {
        vmm_map_page_4kb(virt_addr + i, phys_addr + i, flags);
    }
}

void vmm_init_identity() {
    K_TRY {
        uint64_t* pml4 = (uint64_t*)PML4_ADDR;
        for(int i = 0; i < 512; i++) pml4[i] = 0;

        for (uint64_t addr = 0; addr < 0x100000000ULL; addr += 0x200000) {
            vmm_map_page_2mb_ext(addr, addr, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
        }
        
        uint32_t fb_address = *((uint32_t*)0x508);
        uint64_t fb_aligned = (uint64_t)fb_address & ~0x1FFFFFULL; 
        
        for (uint64_t i = 0; i < 0x2000000; i += 0x200000) {
            uint64_t v_addr = fb_aligned + i;
            vmm_map_page_2mb_ext(v_addr, v_addr, PAGE_PRESENT | PAGE_WRITE | PAGE_PCD | PAGE_USER);
        }

        __asm__ volatile("mov %0, %%cr3" :: "r"((uint64_t)PML4_ADDR) : "memory");
        vga_print_string("\n[VMM] Paginacao Identity e Framebuffer inicializados.", 0, 39);
    }
    K_EXCEPT {
        vga_print_string("\n[VMM CRITICAL] Exceção capturada em vmm_init_identity!", 0, 39);
    }
    K_END_TRY
}

uint64_t read_cr3(void) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

void write_cr3(uint64_t cr3) {
    __asm__ volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
}

int vmm_is_mapped(uint64_t virt_addr) {
    int status = 0;

    K_TRY {
        uint64_t* pml4 = (uint64_t*)(read_cr3() & ~0xFFFULL);

        uint64_t pml4_idx = (virt_addr >> 39) & 0x1FF;
        uint64_t pdpt_idx = (virt_addr >> 30) & 0x1FF;
        uint64_t pd_idx   = (virt_addr >> 21) & 0x1FF;
        uint64_t pt_idx   = (virt_addr >> 12) & 0x1FF;

        if (pml4[pml4_idx] & 1) {
            uint64_t* pdpt = (uint64_t*)(pml4[pml4_idx] & ~0xFFFULL);
            if (pdpt[pdpt_idx] & 1) {
                uint64_t* pd = (uint64_t*)(pdpt[pdpt_idx] & ~0xFFFULL);
                if (pd[pd_idx] & 1) {
                    if (pd[pd_idx] & (1 << 7)) {
                        status = 1;
                    } else {
                        uint64_t* pt = (uint64_t*)(pd[pd_idx] & ~0xFFFULL);
                        if (pt[pt_idx] & 1) status = 1;
                    }
                }
            }
        }
    }
    K_EXCEPT {
        status = 0;
    }
    K_END_TRY

    return status;
}

void vmm_map_page_to_pml4(uint64_t* pml4, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
    virt_addr &= ~0xFFFULL;
    phys_addr &= ~0xFFFULL;

    K_TRY {
        uint64_t pml4_idx = (virt_addr >> 39) & 0x1FF;
        uint64_t pdpt_idx = (virt_addr >> 30) & 0x1FF;
        uint64_t pd_idx   = (virt_addr >> 21) & 0x1FF;
        uint64_t pt_idx   = (virt_addr >> 12) & 0x1FF;

        uint64_t* pdpt = get_next_table(pml4, pml4_idx);
        if (pdpt) {
            uint64_t* pd = get_next_table(pdpt, pdpt_idx);
            if (pd) {
                uint64_t* pt = get_next_table(pd, pd_idx);
                if (pt) {
                    pt[pt_idx] = phys_addr | flags;
                    if ((uint64_t)pml4 == (read_cr3() & ~0xFFFULL)) {
                        __asm__ volatile("invlpg (%0)" :: "r"(virt_addr) : "memory");
                    }
                }
            }
        }
    }
    K_EXCEPT {
        vga_print_string("\n[VMM Fault] Exceção capturada em vmm_map_page_to_pml4!", 0, 39);
    }
    K_END_TRY
}

bool vmm_map_user(uint64_t cr3, uint64_t virt, uint64_t phys) {
    bool result = false;
    uint64_t old_cr3 = read_cr3();

    K_TRY {
        if (old_cr3 != PML4_ADDR) {
            write_cr3(PML4_ADDR);
        }

        uint64_t pml4_idx = (virt >> 39) & 0x1FF;
        uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
        uint64_t pd_idx   = (virt >> 21) & 0x1FF;
        uint64_t pt_idx   = (virt >> 12) & 0x1FF;

        uint64_t* pml4 = (uint64_t*)cr3;

        // --- Nível 1: PML4 -> PDPT ---
        if (!(pml4[pml4_idx] & PAGE_PRESENT)) {
            void* new_tab = pmm_alloc_block();
            if (!new_tab) { 
                vga_print_string("\n[VMM ERROR] Falha ao alocar PDPT no vmm_map_user!", 0, 39);
                if (old_cr3 != PML4_ADDR) write_cr3(old_cr3); 
                return false; 
            }
            memset(new_tab, 0, 4096);
            pml4[pml4_idx] = (uint64_t)new_tab | 0x07; // P | W | U
        }
        uint64_t* pdpt = (uint64_t*)(pml4[pml4_idx] & ~0xFFFULL);

        // --- Nível 2: PDPT -> PD ---
        if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) {
            void* new_tab = pmm_alloc_block();
            if (!new_tab) { 
                vga_print_string("\n[VMM ERROR] Falha ao alocar PD no vmm_map_user!", 0, 39);
                if (old_cr3 != PML4_ADDR) write_cr3(old_cr3); 
                return false; 
            }
            memset(new_tab, 0, 4096);
            pdpt[pdpt_idx] = (uint64_t)new_tab | 0x07; // P | W | U
        }
        uint64_t* pd = (uint64_t*)(pdpt[pdpt_idx] & ~0xFFFULL);

        // --- Nível 3: PD -> PT ---
        if (!(pd[pd_idx] & PAGE_PRESENT)) {
            void* new_tab = pmm_alloc_block();
            if (!new_tab) { 
                vga_print_string("\n[VMM ERROR] Falha ao alocar PT no vmm_map_user!", 0, 39);
                if (old_cr3 != PML4_ADDR) write_cr3(old_cr3); 
                return false; 
            }
            memset(new_tab, 0, 4096);
            pd[pd_idx] = (uint64_t)new_tab | 0x07; // P | W | U
        }
        uint64_t* pt = (uint64_t*)(pd[pd_idx] & ~0xFFFULL);

        // --- Nível 4: PT -> Frame Físico ---
        pt[pt_idx] = (phys & ~0xFFFULL) | 0x07; // P | W | U

        if (old_cr3 != PML4_ADDR) {
            write_cr3(old_cr3);
        }

        __asm__ volatile("invlpg (%0)" :: "r"(virt) : "memory");
        result = true;
    }
    K_EXCEPT {
        vga_print_string("\n[VMM Fault] Exceção capturada em vmm_map_user!", 0, 39);
        if (old_cr3 != PML4_ADDR) write_cr3(old_cr3);
        result = false;
    }
    K_END_TRY

    return result;
}

/**
 * @brief Verifica se um endereço virtual está mapeado em um PML4 específico de forma segura.
 */
int vmm_is_mapped_pml4(uint64_t* pml4, uint64_t virt_addr) {
    int status = 0;

    K_TRY {
        uint64_t pml4_idx = (virt_addr >> 39) & 0x1FF;
        uint64_t pdpt_idx = (virt_addr >> 30) & 0x1FF;
        uint64_t pd_idx   = (virt_addr >> 21) & 0x1FF;
        uint64_t pt_idx   = (virt_addr >> 12) & 0x1FF;

        if (pml4[pml4_idx] & 1) {
            uint64_t* pdpt = (uint64_t*)(pml4[pml4_idx] & ~0xFFFULL);
            if (pdpt[pdpt_idx] & 1) {
                uint64_t* pd = (uint64_t*)(pdpt[pdpt_idx] & ~0xFFFULL);
                if (pd[pd_idx] & 1) {
                    if (pd[pd_idx] & (1 << 7)) {
                        status = 1; // Huge Page 2MB
                    } else {
                        uint64_t* pt = (uint64_t*)(pd[pd_idx] & ~0xFFFULL);
                        if (pt[pt_idx] & 1) status = 1;
                    }
                }
            }
        }
    }
    K_EXCEPT {
        status = 0;
    }
    K_END_TRY

    return status;
}

// =========================================================================
// HANDLER DE PAGE FAULT (EXPANSÃO DINÂMICA DA PILHA EM RING 3)
// =========================================================================
int handle_page_fault(uint64_t user_cr3, uint64_t fault_address) {
    int result = 0;

    K_TRY {
        process_t* current = get_current_process();
        if (current) {
            // 1. Verifica se o acesso está na faixa reservada para expansão da pilha (8MB)
            if (fault_address >= USER_STACK_BOTTOM_LIMIT && fault_address < USER_STACK_TOP) {
                
                uint64_t page_to_alloc = fault_address & ~0xFFFULL;
                uint64_t* process_pml4 = (uint64_t*)user_cr3;

                // 2. Garante que a página ainda não está mapeada
                if (!vmm_is_mapped_pml4(process_pml4, page_to_alloc)) {
                    
                    void* phys_frame = pmm_alloc_block();
                    if (phys_frame) {
                        // 3. Zera o bloco físico DIRETAMENTE antes de mapear
                        memset(phys_frame, 0, 4096);

                        // 4. Mapeia a nova página na PML4 do processo
                        if (vmm_map_user(user_cr3, page_to_alloc, (uint64_t)phys_frame)) {
                            result = 1; // SUCESSO: Pilha expandida com êxito!
                        }
                    }
                }
            }
        }
    }
    K_EXCEPT {
        vga_print_string("\n[VMM Fault] Exceção de Ring 0 capturada em handle_page_fault!", 0, 39);
        result = 0;
    }
    K_END_TRY

    return result; 
}
