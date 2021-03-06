/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Sample/test code for running a user program.  You can use this for
 * reference when implementing the execv() system call. Remember though
 * that execv() needs to do more than this function does.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <thread.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>
#include <test.h>
#include <synch.h>
#include <kern/seek.h>
#include <stat.h> 
#include <fdesc.h>
#include <copyinout.h>

/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname and thus may destroy it.
 */
int
runprogram(char *progname, char **args,long nargs)
{

	vaddr_t stckptr;
	int i = 0;
	int stringbytes=0;
	int membytes[256];
	int numofargs = 0;
	int stacksize = 0;
	int reserror = 0;

	struct vnode *ve;
	vaddr_t enterexecutable;
	
	 
	
	/*Get hold of the file name and validate that stupid thing!!!*/
	    if(progname == NULL){
		return EINVAL;
	    }
	if(strlen(progname) == 0){
		return EINVAL;
	    }

	    if(args != NULL){
		i=0;
	
		while((i) < nargs)
		{
		        if(((strlen(args[i])+1)%4 ) == 0){
		            membytes[i]  =  ((strlen(args[i])+1)/4 ) ;        //gather info for aligned string space
		        }
		        else{
		            membytes[i]  =  ((strlen(args[i])+1)/4 ) +1 ;
		        }

		        i++;
		
		}
		numofargs = i;

		

		for(i=0; i<numofargs; i++){
		
		        stringbytes += membytes[i];
		}

		stacksize = (stringbytes+numofargs+1);
		
	    }

	    /* try and open the program name given to load in execv */
		reserror = vfs_open(progname, O_RDONLY,0,&ve);
		if(reserror)
		{
		   return reserror;
		}

	    /*
	     * Lets start exploiting dumbVM
	     * and its functionalities...
	     */
	    /*destroy the current address space of the thread*/
		if(curthread->t_addrspace)
		{
		    as_destroy(curthread->t_addrspace);
		    curthread->t_addrspace = NULL;
		}

		if(curthread->t_addrspace != NULL){
		    kprintf("dude destroyer failed!!");
		    return -1;
		}


		/* Create new virtual address space */
		curthread->t_addrspace = as_create();
		if(curthread->t_addrspace == NULL)
		{
		    vfs_close(ve);
		    return reserror;
		}

		/* Activate the address space */
		as_activate(curthread->t_addrspace);

		/* Load the ELF file*/
		reserror = load_elf(ve, &enterexecutable);
		if (reserror)
		{
		    vfs_close(ve);
		    return reserror;
		}
		/* file close... */
		vfs_close(ve);

		/* Define the user stack in the address space */
		reserror = as_define_stack(curthread->t_addrspace, &stckptr);
		if (reserror) {
		    return reserror;
		}

		if(args != NULL){

		    vaddr_t intermediateAddrVal[numofargs + 1];
		    intermediateAddrVal[numofargs] = 0;
		    //copying the strings...into the user stack!!
		    for( i = numofargs - 1  ; i >=0 ; i-- ){
		    	stckptr = stckptr - (sizeof(vaddr_t) * membytes[i]);
			reserror = copyout((char *)args[i], (userptr_t)stckptr, (sizeof(vaddr_t) * membytes[i]));
			if(reserror)
			{
			    return reserror;
			}		
			intermediateAddrVal[i] = stckptr;
		     }
		    
		    //copying addresses ... into user stack!! 
		    for( i = numofargs ; i >= 0 ; i-- ){
		    	stckptr = stckptr - sizeof(vaddr_t);
			reserror = copyout(&intermediateAddrVal[i], (userptr_t)stckptr, sizeof(vaddr_t));
			if(reserror)
			{
			    return reserror;
			}		
		     }
		}
		initialize_file_table(curthread);

		/* Warp to user mode */
		enter_new_process(numofargs, (userptr_t)stckptr, stckptr, enterexecutable);
		panic("enter_new_process returned\n");
		return EINVAL;
	
}

void initialize_file_table(struct thread * thread)
{
	int result;
	char * console = NULL;
	struct vnode * vn;
	int i;
	// loop for 3 console file descriptors
	for(i=0;i<3;i++)
	{
		switch(i)
		{	//STDIN
			case 0:
				console = kstrdup("con:");
				result = vfs_open(console,O_WRONLY,0664,&vn);
				kfree(console);
				if(result)
				{
			 		panic("Vfs_open:STDIN to filetable: %s\n",strerror(result));	
				}
	
				thread->t_filetable[i] = (struct fdesc *)kmalloc(sizeof(struct fdesc));	
				strcpy(thread->t_filetable[i]->name,"STDIN");
				break;
			//STDOUT
			case 1:
				console = kstrdup("con:");
				result = vfs_open(console,O_RDONLY,0664,&vn);
				kfree(console);
				if(result)
				{
			 		panic("Vfs_open:STDIN to filetable: %s\n",strerror(result));	
				}
	
				thread->t_filetable[i] = (struct fdesc *)kmalloc(sizeof(struct fdesc));	
				strcpy(thread->t_filetable[i]->name,"STDOUT");
				break;
			//STDERR
			case 2:
				console = kstrdup("con:");
				result = vfs_open(console,O_WRONLY,0664,&vn);
				kfree(console);
				if(result)
				{
			 		panic("Vfs_open:STDIN to filetable: %s\n",strerror(result));	
				}
	
				thread->t_filetable[i] = (struct fdesc *)kmalloc(sizeof(struct fdesc));	
				strcpy(thread->t_filetable[i]->name,"STDERR");
				break;
			default:
				break;
			
		}
		
		if(i < 3)
		{
			if (thread->t_filetable[i] != NULL)
			{
				thread->t_filetable[i]->offset = 0;
				thread->t_filetable[i]->flag = 0;
				thread->t_filetable[i]->ref_count = 1;
				thread->t_filetable[i]->lk = lock_create(thread->t_filetable[i]->name);
				thread->t_filetable[i]->vn = vn;
			}
		}
		
	}	
	

}

