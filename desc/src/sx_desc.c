#include <stdlib.h>
#include <assert.h>

#include "sx_desc.h"

/* -------------------------------------------------
/       sx_desc_get 
/              Get a descriptor and init
/---------------------------------------------------*/
sSX_DESC * sx_desc_get(void){
	
	sSX_DESC	*desc = malloc(sizeof(sSX_DESC));
	assert( desc!= NULL);

	desc->data = NULL;
	desc->data_len = 0;
	desc->next = NULL;

	return desc;
}



/* -------------------------------------------------
/       sx_desc_put 
/              Free a Descriptor or a Descriptor chain
/---------------------------------------------------*/
void sx_desc_put(
	sSX_DESC	*desc){
	
	sSX_DESC	*temp_curr;
	sSX_DESC	*next;
	
	/* Debug to check desc */
	assert(desc != NULL);
	
	temp_curr = desc;
	
	/* Release descriptor and data */
	while( temp_curr!= NULL){
		next = temp_curr->next;
		
		free(temp_curr->data);
		free(temp_curr);
		
		temp_curr = next;
	}
}

