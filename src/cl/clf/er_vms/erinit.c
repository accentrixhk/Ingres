/*
**    Copyright (c) 1986, 2000 Ingres Corporation
*/

#include	<compat.h>
#include	<gl.h>
#include	<er.h>
#include	<st.h>
#include	<me.h>
#include	<nm.h>
#ifndef	VMS
#include	<si.h>	    /* Needed for "erloc.h", if not VMS */
#endif
#include	<cs.h>	    /* Needed for "erloc.h" */
#include	"erloc.h"

/**
**  Name:	erinit.c	- ERinit and support internal routines.
**
**  Description:
**
**	This file defines:
**
**	ERinit		- explicitly initialize the message system.
**
**	cer_isasync	- TRUE if async.  False otherwise.
**	cer_istest	- TRUE if testing.  False otherwise.
**	cer_issem	- TRUE if semaphoing IO.  False otherwise.  If TRUE,
**			  also gives caller pointer to static structure holding
**			  the ER semaphore and the semaphoring functions to
**			  call (those set at ERinit() time).
**
**  History:
**	25-jun-1987 (peter)
**		Written.
**	05-feb-1988 (marge)
**		Modification to cer_istest to test II_MSG_DIR for forms
**		of "yes" and "true" rather than existence of any definition.
**	29-sep-1988 (thurston)
**		Added the cer_issem() routine, the er_sem_funcs static structure
**		and interface changes to ERinit() to allow it to be given ptrs
**		to semaphoring routines.
**	11/11/88 (dkh) - Put in additional check to handle case where
**			 logical II_MSG_TEST is not set at any level.
**			 This prevents the application from crashing.
**      16-jul-93 (ed)
**	    added gl.h
**      11-aug-93 (ed)
**          added missing includes
**	03-aug-1998 (kinte01)
**	    ADD i_sem_func & n_sem_func arguments
**	01-dec-2000	(kinte01)
**	    Bug 103393 - removed nat, longnat, u_nat, & u_longnat
**	    from VMS CL as the use is no longer allowed
**	04-jun-2001 (kinte01)
**	   Replace STbcompare with STcasecmp
*/


/*	Static Declarations  */

#define	NOTSET	0
#define SETON   1
#define SETOFF  2
static	i4	async_state = NOTSET;
static	i4	test_state = NOTSET;
static	i4	sem_set_yet = FALSE;
static	ER_SEMFUNCS	er_sem_funcs;


/*{
**  Name:   ERinit	- Initialize the message system.
**
**  Description:
**	This is used in order to explicitly initialize the message
**	system.  In non-servers, this is done implicitly upon
**	first reference to a message routine.  In a server, a more
**	explicit call can be used in order to set some server-wide
**	information, upon server startup.  Since non-servers can
**	set this information differently for each process, it is
**	easy to use INGRES names for setting various global states,
**	but the server cannot use this technique.
**
**	Three pieces of information are set by this call.
**
**	First, the ER_ASYNC flag records the fact that asynchronous 
**	IO is needed can be set	by one argument.  This allows the server 
**	to avoid blocking on error message reads.
**
**	The second flag, ER_MSGTEST, is used to direct the message
**	system to use individual message files for each class, rather
**	than the default use of one large message file for all messages.
**	In production mode, for best performance, one large message file
**	is used for all slow messages, and one large message file is
**	used for all fast messages.  While developing message files,
**	and while translating message files, however, this is not
**	very convenient, as the compilation of all message files
**	at once can take a significant amount of time.  Instead,
**	by setting this flag, each message class is considered to
**	be in a different set of two files (one fast and one slow),
**	and each time a message is referenced, the correct file is
**	opened and the correct message read.  This, of course, is 
**	much slower than the other method, but quite good for developing
**	new message files.  In this mode, the class message files
**	are identified by the leading 's' for 'slow' or 'f' for
**	fast, followed by the decimal digits representing the class
**	number.  For example, the test message files for class 123
**	would be in files 's123.mnx' and 'f123.mnx'.  If this file 
**	is not found, control reverts to the 'slow.mnx' and 'fast.mnx' 
**	files in the same directory.
**
**	The third flag, ER_SEMAPHORE, is used to supply the ER system with
**	`p' and `v' semaphoring routines to be called before and after IO to
**	the message file.  In a multi-threaded server process (such as the DBMS)
**	this is important.  On some architectures (VMS, for one) a single
**	process cannot have more than one outstanding `read' posted to a given
**	file.  When this flag is set, ERinit() will assume that pointers to the
**	`p' and `v' routines are being supplied, and will set up a static
**	structure to hold them.  At read time, these will be used to prevent
**	one thread from posting an IO when another thread's IO is still in
**	progress.
**
**	For non-servers, which do not explicitly call the ERinit routine, the
**	ER_ASYNC and ER_SEMAPHORE flags are always false.  The test development
**	system can be set by setting the INGRES name II_MSG_TEST to any value.
**
**  Input:
**	flags		Bit string representing the flags to set,
**			which can be ORed together.  The current
**			valid masks are:
**				ER_ASYNC	if async IO is to be used
**				ER_MSGTEST	if individual class message
**						files are to be used.
**				ER_SEMAPHORE	If IO calls to the message file
**						are to be semaphore protected.
**						Setting this flag will tell
**						ERinit() that the following two
**						arguments are to be pointers to
**						a CSp_semaphore() routine and a
**						a CSv_semaphore() routine.
**	p_sem_func	Only used if the ER_SEMAPHORE flag is set.  Pointer to
**			a CSp_semaphore() look alike routine, that will be used
**			to request a semaphore before doing an IO to the message
**			file.
**	v_sem_func	Only used if the ER_SEMAPHORE flag is set.  Pointer to
**			a CSv_semaphore() look alike routine, that will be used
**			to release a semaphore after doing an IO to the message
**			file.
**
**  Output:
**	Return:
**		VOID.
**
**  Side effect:
**	Sets the values of some static variables async_state and test_state,
**	which are used in the internal routines.
**	ERp_semaphore, and ERv_semaphore
**	routines.
**
**  History:
**	4-May-1987 (peter)
**	    First written for 5.0 KANJI.
**	29-sep-1987 (thurston)
**	    Added the ER_SEMAPHORE flag, and p_sem_func and v_sem_func arguments
**	    to allow server to semaphore around IO's to the message file.
**	03-aug-1998 (kinte01)
**	    ADD i_sem_func & n_sem_func arguments
*/

VOID
ERinit(flags, p_sem_func, v_sem_func, i_sem_func, n_sem_func)
i4		flags;
STATUS		(*p_sem_func)();
STATUS		(*v_sem_func)();
STATUS          (*i_sem_func)();
VOID            (*n_sem_func)();
{
	async_state = (flags & ER_ASYNC) ? SETON : SETOFF;
	test_state = (flags & ER_MSGTEST) ? SETON : SETOFF;
	if (flags & ER_SEMAPHORE)
	{
	    MEfill((u_i2)sizeof(er_sem_funcs), (u_char)0, (PTR)&er_sem_funcs);
            if (i_sem_func != NULL )
            {
                (*i_sem_func)(&er_sem_funcs.er_sem, CS_SEM_SINGLE);
                if (n_sem_func)
                    (*n_sem_func)(&er_sem_funcs.er_sem, "ER IO sem");
            }
	    er_sem_funcs.er_p_semaphore = p_sem_func;
	    er_sem_funcs.er_v_semaphore = v_sem_func;
	    sem_set_yet = TRUE;
	}

	return;
}


/*{
**  Name:	cer_isasync	- Test for whether Async is on or off.
**
**  Description:
**	Looks at the static variable to return whether or not the value
**	has been set.  If NOTSET, default is off.
**
**  Inputs:
**	None:
**
**  Outputs:
**	TRUE	if set.
**	FALSE	if not set, or set to off.
**
**
**  History:
**	25-jun-1987 (peter)
**		Written.
*/

bool
cer_isasync()
{
	return ((async_state == SETON) ? TRUE : FALSE);
}


/*{
**  Name:	cer_istest	- Test whether test message system is set.
**
**  Description:
**	Looks at the static variable to return whether or not the value
**	has been set.  If NOTSET, default is to check for the INGRES
**	name of II_MSG_TEST.  If that is set to any variation of TRUE
**	or YES, then the test system is on, and all message access is 
**	to be done through individual class files 
**	(as described in the header for ERinit).
**
**  Inputs:
**	None:
**
**  Outputs:
**	TRUE	if set, or if NOTSET, and a check of II_MSG_TEST is set
**		to some form of yes or true
**	FALSE	if not set, or set to NOTSET and a check of II_MSG_TEST
**		fails.
**
**  Side effect:
**	Sets the values of static variable test_state.
**
**  History:
**	25-jun-1987 (peter)
**		Written.
**	05-feb-1988 (marge)
**		Changed II_MSG_TEST to test for trues or yeses
**		instead of definition only
**	04-jun-2001 (kinte01)
**	   Replace STbcompare with STcasecmp
*/

bool
cer_istest()
{
	char	*nambuf;

	if (test_state == NOTSET)
	{   /* Check the II_MSG_TEST */
		NMgtAt("II_MSG_TEST", &nambuf);
		if (nambuf != NULL
		   && (STcasecmp(nambuf, "y") == 0
		      || STcasecmp(nambuf, "yes") == 0
		      || STcasecmp(nambuf, "t") == 0
		      || STcasecmp(nambuf, "true") == 0))
		{	/* Value is Set */
			test_state = SETON;
		}
		else
		{
			test_state = SETOFF;
		}
	}

	return ((test_state == SETON) ? TRUE : FALSE);
}


/*{
**  Name: cer_issem - Test whether semaphoring IO to message file is being done.
**
**  Description:
**	Looks at the static variables to determine whether or not there are
**	function pointers to semaphore request and release functions.  If both
**	are non-NULL pointers, this function will return TRUE, along with a
**	pointer to the static structure holding them and the ER semaphore.
**	Otherwise, FALSE will be returned, and the sem_funcs pointer will be
**	set to NULL.
**
**  Inputs:
**	None:
**
**  Outputs:
**	sem_funcs	If semaphoring is being done, this will be set to point
**			to the static structure holding the ER semaphore and the
**			semaphore function pointers.  If not, it will be set to
**			NULL.
**
**  Returns:
**	TRUE		If semaphoring is being done.
**	FALSE		If semaphoring is *NOT* being done.
**
**  Side effect:
**	none.
**
**  History:
**	29-sep-88 (thurston)
**	    Written.
*/

bool
cer_issem(sem_funcs)
ER_SEMFUNCS	**sem_funcs;
{
    if (!sem_set_yet)
    {
	MEfill((u_i2)sizeof(er_sem_funcs), (u_char)0, (PTR)&er_sem_funcs);
	sem_set_yet = TRUE;
    }
    
    if (er_sem_funcs.er_p_semaphore && er_sem_funcs.er_p_semaphore)
    {
	*sem_funcs = &er_sem_funcs;
	return (TRUE);
    }
    else
    {
	*sem_funcs = NULL;
	return (FALSE);
    }
}
