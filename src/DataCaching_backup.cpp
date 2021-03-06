///
/// \author Anastasiadis Petros (panastas@cslab.ece.ntua.gr)
///
/// \brief The CoCopeLia caching functions.
///

//#include <cassert>
//#include <atomic>

#include "unihelpers.hpp"
#include "DataCaching.hpp"
#include "Asset.hpp"
#include "backend_wrappers.hpp"

DevCachePtr DevCache[128] = {NULL};
int globalock = 0;
short recursion_depth[128] = {0};

#ifdef STEST
double total_cache_timer[LOC_NUM] = {0};
#endif

#if CACHE_SCHEDULING_POLICY==1 || CACHE_SCHEDULING_POLICY==2 || CACHE_SCHEDULING_POLICY==3
LinkedList::LinkedList(){
	start = NULL;
	end = NULL;
	length = 0;
	iter = NULL;
}

void LinkedList::invalidate(Node_LL* node){
	if(node == NULL)
		error("LinkedList_Invalidate: Node not found.");
	else
		node->valid = false;
}

void LinkedList::push_back(int idx){
	// Pushes an element in the back of the queue.
	Node_LL* tmp = new Node_LL();
	tmp->next = NULL;
	tmp->previous = NULL;
	tmp->idx = idx;
	tmp->valid = true;
	if(start == NULL){ // Queue is empty.
		start = tmp;
		end = tmp;

	}
	else{ // Queue not empty.
		end->next = tmp;
		tmp->previous = end;
		end = tmp;
	}
	length++;
}


Node_LL* LinkedList::start_iterration(){
	short lvl = 6;
	// Returns the first valid element without removing it.
	if(start == NULL) // Queue is empty.
		error("LinkedList_start_iterration: Fifo queue is empty. Can't pop element.\n");
	else{ // Queue not empty.
		iter = start;
		while(iter != NULL && !iter->valid)
			iter = iter->next;
		if(iter == NULL){
			Node_LL* tmp_node = new Node_LL();
			tmp_node->idx = -1;
			return tmp_node;
		}
		else
			return iter;
	}
}

Node_LL* LinkedList::next_in_line(){
	// Returns next element in iterration.
	short lvl = 6;
	if(iter == NULL)
		error("LinkedList_next_in_line: Iterration not started. Call check_first.\n");
	else{
		iter = iter->next;
		while(iter != NULL && !iter->valid)
			iter = iter->next;
		if(iter == NULL){
			Node_LL* tmp_node = new Node_LL();
			tmp_node->idx = -1;
			return tmp_node;
		}
		else
			return iter;
	}
}

int LinkedList::remove(Node_LL* node){
	// Removes first element.
	int tmp_idx;
	Node_LL* tmp_node;

	if(node == NULL) // Node not found.
		error("LinkedList_remove: Input node not found. Can't pop element.\n");
	else{
		tmp_idx = node->idx;
		tmp_node = node;
		(tmp_node->previous)->next = tmp_node->next;
		free(tmp_node);
		length--;
		return tmp_idx;
	}
}

void LinkedList::put_first(Node_LL* node){
	// Puts the element first on the list.
	if(node == NULL)
		error("LinkedList_put_first: Input node not found. Can't move element.\n");
	else if(node->idx<0)
		error("LinkedList_put_first: Input node is invalid. Can't move element.\n");
	else if(node->previous==NULL){
		// Node is the first on the list.
		if(start==node)
			node->valid = true;
		// Node is not on the list because it can't be inside with no next.
		else if(node->next==NULL){
		start->previous = node;
		node->next = start;
		start = node;
		node->valid = true;
		}
		else
			error("LinkedList_put_first: Unexpected internal error.\n");
	}// Node is somewhere inside the list but not last.
	else{
		if(end==node)
			end=node->previous;

		(node->previous)->next = node->next;
		if(node->next != NULL)
			(node->next)->previous = node->previous;
		start->previous = node;
		node->next = start;
		node->previous = NULL;
		node->valid = true;
		start = node;
	}
}

void LinkedList::put_last(Node_LL* node){
	// Takes a node and puts it in the end of the list.
	if(node == NULL)
		error("LinkedList_put_last: Input node not found. Can't move element.\n");
	else if(node->idx<0)
		error("LinkedList_put_last: Input node is invalid. Can't move element.\n");
	else if(node->next==NULL){
		// Node is the last on the list.
		if(end==node)
			node->valid = true;
		// Node is not on the list because it can't be inside with no next.
		else if(node->previous==NULL){
			end->next = node;
			node->previous = end;
			end = node;
			node->valid = true;
		}
		else
			error("LinkedList_put_last: Unexpected internal error.\n");
	} // Node is somewhere inside the list but not last.
	else{
		if(node==start)
			start = node->next;

		(node->next)->previous = node->previous;
		if(node->previous != NULL)
			(node->previous)->next = node->next;
		end->next = node;
		node->previous = end;
		node->next = NULL;
		node->valid = true;
		end = node;
	}
}


LinkedList::~LinkedList(){
	Node_LL* tmp;
	while(start != NULL){
		tmp = start;
		start = tmp->next;
		free(tmp);
	}
}
#endif

#if CACHE_SCHEDULING_POLICY==1
LinkedList fifo_queues[128];
#elif CACHE_SCHEDULING_POLICY==2 || CACHE_SCHEDULING_POLICY==3
Node_LL** mru_lru_hash[128];
LinkedList mru_lru_queues[128];
#endif


const char* print_state(state in_state){
	switch(in_state){
		case(EMPTY):
			return "EMPTY";
		case(FETCHING):
			return "FETCHING";
		case(AVAILABLE):
			return "AVAILABLE";
		case(R):
			return "R";
		case(EXCLUSIVE):
			return "EXCLUSIVE";
		default:
			error("print_state: Unknown state\n");
	}
}


DevCachePtr CoCoPeLiaDevCacheInit(short dev_id, long long block_num, long long block_size){
  DevCachePtr result = (DevCachePtr) malloc (sizeof(struct DevCache_str));
  short dev_id_idx = idxize(dev_id);
  result->gpu_mem_buf = NULL;
  result->mem_buf_sz = result->serialCtr = 0;
	result->BlockSize = block_size;
	result->BlockNum = block_num;
	result->mem_buf_sz= result->BlockSize*result->BlockNum;
	result->BlockState = (state*) calloc (result->BlockNum, sizeof(state));
	result->BlockReaders = (int*) calloc (result->BlockNum, sizeof(int));
	result->BlockCurrentTileDim = (short*) calloc (result->BlockNum, sizeof(short));
	result->BlockCurrentTilePtr = (void**) calloc (result->BlockNum, sizeof(void*));
	return result;
}

void CoCopeLiaDevCacheFree(short dev_id)
{
	short lvl = 3;
#ifdef DEBUG
			lprintf(lvl-1, "|-----> CoCopeLiaDevCacheFree(dev_id=%d)\n", dev_id);
#endif
  short dev_id_idx = (dev_id >= 0) ? dev_id : LOC_NUM - 1;
#ifdef TEST
	double cpu_timer = csecond();
#endif
	if (DevCache[dev_id_idx]){
#ifdef DEBUG
		lprintf(lvl, "Clearing (presumably) %zu MB\n\n", (size_t) DevCache[dev_id_idx]->mem_buf_sz/1024/1024);
#endif
		CoCoFree(DevCache[dev_id_idx]->gpu_mem_buf, dev_id);
		free(DevCache[dev_id_idx]->BlockState);
		free(DevCache[dev_id_idx]->BlockReaders);
		free(DevCache[dev_id_idx]->BlockCurrentTileDim);
		free(DevCache[dev_id_idx]->BlockCurrentTilePtr);
		free(DevCache[dev_id_idx]);
		DevCache[dev_id_idx] = NULL;
    recursion_depth[dev_id_idx] = 0;
		CoCoSyncCheckErr();
	}
	else{
#ifdef DEBUG
		lprintf(lvl, "Target buffer already empty\n");
#endif
		;
	}
#ifdef TEST
	cpu_timer = csecond() - cpu_timer;
	lprintf(lvl, "Buffer de-allocation(%d): t_free = %lf ms\n" , dev_id, cpu_timer*1000);
#endif
#ifdef DEBUG
	lprintf(lvl-1, "<-----|\n");
#endif
}

void CoCoPeLiaDevCacheInvalidate(kernel_pthread_wrap_p subkernel_data){
	short lvl = 3;
#ifdef DEBUG
	lprintf(lvl-1, "|-----> CoCoPeLiaDevCacheInvalidate(subkernel_list(len=%d)\n", subkernel_data->SubkernelNumDev);
#endif
#ifdef TEST
	double cpu_timer = csecond();
#endif
	for (int i = 0; i < subkernel_data->SubkernelNumDev; i++){
		Subkernel* curr = subkernel_data->SubkernelListDev[i];
		short dev_id_idx = idxize(curr->run_dev_id);
		for (int j = 0; j < curr->TileNum; j++){
			if (curr->TileDimlist[j] == 1) {
					Tile1D<VALUE_TYPE>* tmp = (Tile1D<VALUE_TYPE>*) curr->TileList[j];
					if (tmp->CacheLocId[dev_id_idx] != -1) tmp->CacheLocId[dev_id_idx] = -42;
					tmp->available[dev_id_idx]->reset();
			}
			else if (curr->TileDimlist[j] == 2){
					Tile2D<VALUE_TYPE>* tmp = (Tile2D<VALUE_TYPE>*) curr->TileList[j];
					if (tmp->CacheLocId[dev_id_idx] != -1) tmp->CacheLocId[dev_id_idx] = -42;
					tmp->available[dev_id_idx]->reset();
			}
			else error("CoCoPeLiaDevCacheInvalidate: Not implemented for TileDim=%d\n", curr->TileDimlist[j]);
		}
		 if (DevCache[dev_id_idx]!= NULL && DevCache[dev_id_idx]->serialCtr){
			 DevCache[dev_id_idx]->serialCtr = 0;
			 recursion_depth[dev_id_idx] = 0;
			 for (int idx = 0; idx < DevCache[dev_id_idx]->BlockNum; idx++){
				 DevCache[dev_id_idx]->BlockReaders[idx] = 0;
				 DevCache[dev_id_idx]->BlockState[idx] = EMPTY;
				 DevCache[dev_id_idx]->BlockCurrentTilePtr[idx] = NULL;
		 	}
		}
	}
#ifdef TEST
	cpu_timer = csecond() - cpu_timer;
	lprintf(lvl, "Cache for %d Subkernels invalidated: t_nv = %lf ms\n" , subkernel_data->SubkernelNumDev, cpu_timer*1000);
#endif
#ifdef DEBUG
	lprintf(lvl-1, "<-----|\n");
#endif
}

void* CacheAsignBlock(short dev_id, void* TilePtr, short TileDim){

  short lvl = 5;
	int* block_id_ptr;
	short dev_id_idx = (dev_id == -1) ? LOC_NUM - 1: dev_id;
#ifdef STEST
	total_cache_timer[dev_id_idx]-= csecond();
#endif
	if (TileDim == 1){
		Tile1D<VALUE_TYPE>* tmp = (Tile1D<VALUE_TYPE>*) TilePtr;
		block_id_ptr = &tmp->CacheLocId[dev_id_idx];
#ifdef DEBUG
  	lprintf(lvl-1, "|-----> CacheAsignBlock(dev= %d, T = %d.[%d] )\n",
			dev_id, tmp->id, tmp->GridId);
#endif
	}
	else if(TileDim == 2){
		Tile2D<VALUE_TYPE>* tmp = (Tile2D<VALUE_TYPE>*) TilePtr;
		block_id_ptr = &tmp->CacheLocId[dev_id_idx];
#ifdef DEBUG
  	lprintf(lvl-1, "|-----> CacheAsignBlock(dev= %d, T = %d.[%d,%d] )\n",
			dev_id, tmp->id, tmp->GridId1, tmp->GridId2);
#endif
	}
	else error("CacheAsignBlock(%d): Tile%dD not implemented\n", dev_id, TileDim);
	if (DevCache[dev_id_idx] == NULL)
		error("CacheAsignBlock(%d): Called on empty buffer\n", dev_id);
	void* result = NULL;
  if (DevCache[dev_id_idx]->serialCtr >= DevCache[dev_id_idx]->BlockNum){
#ifdef STEST
		total_cache_timer[dev_id_idx]+= csecond();
#endif
    return CacheUpdateAsignBlock(dev_id, TilePtr, TileDim);
	}
	else{
 #if CACHE_SCHEDULING_POLICY==1
		fifo_queues[dev_id_idx].lock_ll.lock();
#elif CACHE_SCHEDULING_POLICY==2 || CACHE_SCHEDULING_POLICY==3
		mru_lru_queues[dev_id_idx].lock_ll.lock();
#endif
  	result = DevCache[dev_id_idx]->gpu_mem_buf + DevCache[dev_id_idx]->serialCtr*DevCache[dev_id_idx]->BlockSize;
		*block_id_ptr = DevCache[dev_id_idx]->serialCtr;
		DevCache[dev_id_idx]->BlockCurrentTileDim[DevCache[dev_id_idx]->serialCtr] = TileDim;
		DevCache[dev_id_idx]->BlockCurrentTilePtr[DevCache[dev_id_idx]->serialCtr] = TilePtr;
#if CACHE_SCHEDULING_POLICY==1
		fifo_queues[dev_id_idx].push_back(DevCache[dev_id_idx]->serialCtr);
		fifo_queues[dev_id_idx].lock_ll.unlock();
#elif CACHE_SCHEDULING_POLICY==2
		mru_lru_queues[dev_id_idx].push_back(DevCache[dev_id_idx]->serialCtr);
		mru_lru_hash[dev_id_idx][DevCache[dev_id_idx]->serialCtr] = mru_lru_queues[dev_id_idx].end;
		mru_lru_queues[dev_id_idx].put_first(mru_lru_hash[dev_id_idx][DevCache[dev_id_idx]->serialCtr]);
		mru_lru_queues[dev_id_idx].lock_ll.unlock();
#elif CACHE_SCHEDULING_POLICY==3
		mru_lru_queues[dev_id_idx].push_back(DevCache[dev_id_idx]->serialCtr);
		mru_lru_hash[dev_id_idx][DevCache[dev_id_idx]->serialCtr] = mru_lru_queues[dev_id_idx].end;
		mru_lru_queues[dev_id_idx].lock_ll.unlock();
#endif
		DevCache[dev_id_idx]->serialCtr++;
	}
#ifdef DEBUG
	lprintf(lvl-1, "<-----|\n");
#endif
#ifdef STEST
	total_cache_timer[dev_id_idx]+= csecond();
#endif
  return result;
}

void* CacheUpdateAsignBlock(short dev_id, void* TilePtr, short TileDim){
	short lvl = 5;
	int* block_id_ptr;
	short dev_id_idx = (dev_id == -1) ? LOC_NUM - 1: dev_id;
#ifdef STEST
	total_cache_timer[dev_id_idx]-= csecond();
#endif
	if (TileDim == 1){
		Tile1D<VALUE_TYPE>* tmp = (Tile1D<VALUE_TYPE>*) TilePtr;
		block_id_ptr = &tmp->CacheLocId[dev_id_idx];
#ifdef DEBUG
  	lprintf(lvl-1, "|-----> CacheUpdateAsignBlock(dev= %d, T = %d.[%d] )\n",
			dev_id, tmp->id, tmp->GridId);
#endif
	}
	else if(TileDim == 2){
		Tile2D<VALUE_TYPE>* tmp = (Tile2D<VALUE_TYPE>*) TilePtr;
		block_id_ptr = &tmp->CacheLocId[dev_id_idx];
#ifdef DEBUG
  	lprintf(lvl-1, "|-----> CacheUpdateAsignBlock(dev= %d, T = %d.[%d,%d] )\n",
			dev_id, tmp->id, tmp->GridId1, tmp->GridId2);
#endif
	}
	else error("CacheUpdateAsignBlock(%d): Tile%dD not implemented\n", dev_id, TileDim);
	if (DevCache[dev_id_idx] == NULL)
		error("CacheUpdateAsignBlock(%d): Called on empty buffer\n", dev_id);
	while(__sync_lock_test_and_set (&globalock, 1));
	void* result = NULL;
#if CACHE_SCHEDULING_POLICY==0
	int remove_block_idx = CacheSelectBlockToRemove_naive(dev_id);
	if(remove_block_idx >= 0){
		result = DevCache[dev_id_idx]->gpu_mem_buf + remove_block_idx*DevCache[dev_id_idx]->BlockSize;
		DevCache[dev_id_idx]->BlockState[remove_block_idx] = EMPTY;

#elif CACHE_SCHEDULING_POLICY==1
	Node_LL* remove_block;
	remove_block = CacheSelectBlockToRemove_fifo(dev_id);
	if(remove_block->idx >= 0){
		fifo_queues[dev_id_idx].lock_ll.lock();
		int remove_block_idx = fifo_queues[dev_id_idx].remove(remove_block);
		fifo_queues[dev_id_idx].push_back(remove_block_idx);
		fifo_queues[dev_id_idx].lock_ll.unlock();
		result = DevCache[dev_id_idx]->gpu_mem_buf + remove_block_idx*DevCache[dev_id_idx]->BlockSize;
		DevCache[dev_id_idx]->BlockState[remove_block_idx] = EMPTY;

#elif CACHE_SCHEDULING_POLICY==2 || CACHE_SCHEDULING_POLICY==3
	Node_LL* remove_block;
	remove_block = CacheSelectBlockToRemove_mru_lru(dev_id);
	if(remove_block->idx >= 0){
		mru_lru_queues[dev_id_idx].lock_ll.lock();
		int remove_block_idx = remove_block->idx;
	#if CACHE_SCHEDULING_POLICY==2
		mru_lru_queues[dev_id_idx].put_first(remove_block);
	#elif CACHE_SCHEDULING_POLICY==3
		mru_lru_queues[dev_id_idx].put_last(remove_block);
	#endif
		mru_lru_queues[dev_id_idx].lock_ll.unlock();
		result = DevCache[dev_id_idx]->gpu_mem_buf + remove_block_idx*DevCache[dev_id_idx]->BlockSize;
		DevCache[dev_id_idx]->BlockState[remove_block_idx] = EMPTY;
#endif
		if (TileDim == 1){
			Tile1D<VALUE_TYPE>* prev_tile = (Tile1D<VALUE_TYPE>*)
				DevCache[dev_id_idx]->BlockCurrentTilePtr[remove_block_idx];
			prev_tile->CacheLocId[dev_id_idx] = -42;
			prev_tile->available[dev_id_idx]->reset();
		#ifdef CDEBUG
				lprintf(lvl, "CacheSelectBlockToRemove(%d): Replacing Block(idx=%d) - T(%d.[%d])...\n",
				dev_id, remove_block_idx, prev_tile->id, prev_tile->GridId);
		#endif
		}
		else if(TileDim == 2){
			Tile2D<VALUE_TYPE>* prev_tile = (Tile2D<VALUE_TYPE>*)
				DevCache[dev_id_idx]->BlockCurrentTilePtr[remove_block_idx];
			prev_tile->CacheLocId[dev_id_idx] = -42;
			prev_tile->available[dev_id_idx]->reset();
		#ifdef CDEBUG
				lprintf(lvl, "CacheSelectBlockToRemove(%d): Replacing Block(idx=%d)- T(%d.[%d,%d])...\n",
				dev_id, remove_block_idx, prev_tile->id, prev_tile->GridId1, prev_tile->GridId2);
		#endif
		}
		else error("CacheUpdateAsignBlock(%d): Tile%dD not implemented\n", dev_id, TileDim);
		*block_id_ptr = remove_block_idx;
		DevCache[dev_id_idx]->BlockCurrentTileDim[remove_block_idx] = TileDim;
		DevCache[dev_id_idx]->BlockCurrentTilePtr[remove_block_idx] = TilePtr;
	}
	else{
#if CACHE_SCHEDULING_POLICY==1 || CACHE_SCHEDULING_POLICY==2 || CACHE_SCHEDULING_POLICY==3
		free(remove_block);
#endif

		error("CacheUpdateAsignBlock(dev= %d)-Rec: entry\n",dev_id);
		__sync_lock_release(&globalock);
#ifdef STEST
		total_cache_timer[dev_id_idx]+= csecond();
#endif
		return CacheUpdateAsignBlock(dev_id, TilePtr, TileDim);
	}
#ifdef DEBUG
	lprintf(lvl-1, "<-----|\n");
#endif
		__sync_lock_release(&globalock);
#ifdef STEST
		total_cache_timer[dev_id_idx]+= csecond();
#endif
  return result;
}

#if CACHE_SCHEDULING_POLICY==0
int CacheSelectBlockToRemove_naive(short dev_id){
	short lvl = 6;
#ifdef DEBUG
	lprintf(lvl-1, "|-----> CacheSelectBlockToRemove_naive(dev= %d)\n",dev_id);
#endif
#ifdef CTEST
	cpu_timer = csecond();
#endif
	short dev_id_idx = (dev_id == -1) ? LOC_NUM - 1: dev_id;
	int result_idx = -1;
	if (DevCache[dev_id_idx] == NULL)
		error("CacheSelectBlockToRemove_naive(%d): Called on empty buffer\n", dev_id);
	for (int idx = 0; idx < DevCache[dev_id_idx]->BlockNum; idx++){ // Iterate through cache serially.
		if(DevCache[dev_id_idx]->BlockState[idx] == AVAILABLE){ 		// Index can be removed if block is available
			result_idx =  idx;
			break;
		}
	}
#ifdef CTEST
	cpu_timer = csecond() - cpu_timer;
	lprintf(lvl-1, "CacheSelectBlockToRemove_naive(%d): t_select = %lf\n",dev_id, cpu_timer);
	cpu_timer = csecond();
#endif
#ifdef DEBUG
	lprintf(lvl-1, "<-----|\n");
#endif
	return result_idx;
}

#elif CACHE_SCHEDULING_POLICY==1
Node_LL* CacheSelectBlockToRemove_fifo(short dev_id){
	short lvl = 6;
#ifdef DEBUG
	lprintf(lvl-1, "|-----> CacheSelectBlockToRemove_fifo(dev= %d)\n",dev_id);
#endif
#ifdef CTEST
	cpu_timer = csecond();
#endif
	Node_LL* result_node = new Node_LL();

  short dev_id_idx = (dev_id >= 0) ? dev_id : LOC_NUM - 1;
	result_node->idx = -1;
	if (DevCache[dev_id_idx] == NULL)
		error("CacheSelectBlockToRemove_fifo(%d): Called on empty buffer\n", dev_id);
	// while(__sync_lock_test_and_set (&globalock, 1));
	fifo_queues[dev_id_idx].lock_ll.lock();
	Node_LL* node = fifo_queues[dev_id_idx].start_iterration();
	while(DevCache[dev_id_idx]->BlockState[node->idx] != AVAILABLE){
		node = fifo_queues[dev_id_idx].next_in_line();
		if(node->idx < 0) break;
	}
	if(node->idx >=0 && DevCache[dev_id_idx]->BlockState[node->idx] == AVAILABLE){
		lprintf(lvl-1, "|-----> CacheSelectBlockToRemove_fifo: Invalidating\n");
		fifo_queues[dev_id_idx].invalidate(node);
		free(result_node);
		result_node = node;
	}
	fifo_queues[dev_id_idx].lock_ll.unlock();
	// __sync_lock_release(&globalock);

#ifdef CTEST
	cpu_timer = csecond() - cpu_timer;
	lprintf(lvl-1, "CacheSelectBlockToRemove_fifo(%d): t_select = %lf\n",dev_id, cpu_timer);
	cpu_timer = csecond();
#endif
#ifdef DEBUG
	lprintf(lvl-1, "<-----|\n");
#endif
	return result_node;
}

#elif CACHE_SCHEDULING_POLICY==2 || CACHE_SCHEDULING_POLICY==3
Node_LL* CacheSelectBlockToRemove_mru_lru(short dev_id){
short lvl = 6;
#ifdef DEBUG
	lprintf(lvl-1, "|-----> CacheSelectBlockToRemove_mru_lru(dev= %d)\n",dev_id);
#endif
#ifdef CTEST
	cpu_timer = csecond();
#endif

Node_LL* result_node = new Node_LL();

	result_node->idx = -1;
	short dev_id_idx = (dev_id == -1) ? LOC_NUM - 1: dev_id;

	if (DevCache[dev_id_idx] == NULL)
		error("CacheSelectBlockToRemove_mru_lru(%d): Called on empty buffer\n", dev_id);
	mru_lru_queues[dev_id_idx].lock_ll.lock();
	Node_LL* node = mru_lru_queues[dev_id_idx].start_iterration();
	while(DevCache[dev_id_idx]->BlockState[node->idx] != AVAILABLE){
		node = mru_lru_queues[dev_id_idx].next_in_line();
		if(node->idx < 0) break;
	}
	if(node->idx >=0 && DevCache[dev_id_idx]->BlockState[node->idx] == AVAILABLE){
		// lprintf(lvl-1, "|-----> CacheSelectBlockToRemove_mru_lru: Invalidating\n");
		mru_lru_queues[dev_id_idx].invalidate(node);
		free(result_node);
		result_node = node;
	}
	mru_lru_queues[dev_id_idx].lock_ll.unlock();

#ifdef CTEST
	cpu_timer = csecond() - cpu_timer;
	lprintf(lvl-1, "CacheSelectBlockToRemove_mru(%d): t_select = %lf\n",dev_id, cpu_timer);
	cpu_timer = csecond();
#endif
#ifdef DEBUG
	lprintf(lvl-1, "<-----|\n");
#endif
	return result_node;
}
#endif

void CacheGetLock(void*){
		while(__sync_lock_test_and_set (&globalock, 1));
}

void CacheReleaseLock(void*){
	__sync_lock_release(&globalock);
}


void CacheCheckCorrect(short dev_id, int BlockIdx){
	short dev_id_idx = idxize(dev_id);
	if (DevCache[dev_id_idx] == NULL)
		error("CacheCheckCorrect(%d,%d): Called on empty buffer\n", dev_id, BlockIdx);
	if (BlockIdx < 0 || BlockIdx >= DevCache[dev_id_idx]->BlockNum)
		error("CacheCheckCorrect(%d,%d): Invalid BlockIdx = %d\n", dev_id, BlockIdx, BlockIdx);
}

state CacheGetBlockStateNoLock(short dev_id, int BlockIdx){
	short dev_id_idx = idxize(dev_id);
	CacheCheckCorrect(dev_id, BlockIdx);
	state result = DevCache[dev_id_idx]->BlockState[BlockIdx];
	return result;
}

state CacheSetBlockState(short dev_id, int BlockIdx, state new_state){
	short dev_id_idx = idxize(dev_id);
	state result = DevCache[dev_id_idx]->BlockState[BlockIdx];
	DevCache[dev_id_idx]->BlockState[BlockIdx] = new_state;
	return result;
}

state CacheUpdateBlockState(short dev_id, int BlockIdx){
	short dev_id_idx = idxize(dev_id);
	state prev_state = DevCache[dev_id_idx]->BlockState[BlockIdx];
	if(DevCache[dev_id_idx]->BlockReaders[BlockIdx] > 0){
		if (prev_state!=R && prev_state!=AVAILABLE)
			error("CacheUpdateBlockState(dev_id=%d, BlockIdx=%d): Block with %d readers has state %s\n",
				dev_id, BlockIdx, DevCache[dev_id_idx]->BlockReaders[BlockIdx],
				print_state(prev_state));
		else 	DevCache[dev_id_idx]->BlockState[BlockIdx] = R;
	}
	else if (prev_state==R && DevCache[dev_id_idx]->BlockReaders[BlockIdx] < 0)
		error("CacheUpdateBlockState(dev_id=%d, BlockIdx=%d): Block with %d readers has state R\n",
					dev_id, BlockIdx, DevCache[dev_id_idx]->BlockReaders[BlockIdx]);
	else if (prev_state==R && DevCache[dev_id_idx]->BlockReaders[BlockIdx] == 0)
		DevCache[dev_id_idx]->BlockState[BlockIdx] = AVAILABLE;
	return prev_state;
}

void CacheInvalidate(void* wrapped_CacheWrap_p){
	short lvl = 5;
	CacheWrap_p unwrap = (CacheWrap_p) wrapped_CacheWrap_p;
	int BlockIdx = unwrap->BlockIdx;
	short dev_id = unwrap->dev_id, dev_id_idx = idxize(unwrap->dev_id);
#ifdef STEST
	total_cache_timer[dev_id_idx]-= csecond();
#endif
	CacheCheckCorrect(dev_id, BlockIdx);
	if(unwrap->lock_flag) while(__sync_lock_test_and_set (&globalock, 1));

	if(DevCache[dev_id_idx]->BlockReaders[BlockIdx]!=0)
			error("CacheInvalidate(dev_id=%d, BlockIdx=%d): Still has %d readers\n",
				dev_id, BlockIdx, DevCache[dev_id_idx]->BlockReaders[BlockIdx]);

	state prev_state = CacheSetBlockState(dev_id, BlockIdx, EMPTY);
	if (prev_state!= AVAILABLE && prev_state!= EMPTY && prev_state!= EXCLUSIVE)
		error("CacheInvalidate(dev_id=%d, BlockIdx=%d) prev_state = %s\n",
			dev_id, BlockIdx, print_state(prev_state));
	if(unwrap->lock_flag) __sync_lock_release(&globalock);
#ifdef DEBUG
  lprintf(lvl, "CacheInvalidate(dev_id=%d, BlockIdx=%d) ran succesfully.\n", dev_id, BlockIdx);
#endif
#ifdef STEST
	total_cache_timer[dev_id_idx]+= csecond();
#endif
}

void CacheStartRead(void* wrapped_CacheWrap_p){
	short lvl = 5;
	CacheWrap_p unwrap = (CacheWrap_p) wrapped_CacheWrap_p;
	int BlockIdx = unwrap->BlockIdx;
	short dev_id = unwrap->dev_id, dev_id_idx = idxize(unwrap->dev_id);
#ifdef STEST
	total_cache_timer[dev_id_idx]-= csecond();
#endif
	CacheCheckCorrect(dev_id, BlockIdx);

	if(unwrap->lock_flag) while(__sync_lock_test_and_set (&globalock, 1));
		DevCache[dev_id_idx]->BlockReaders[BlockIdx]++;
	CacheUpdateBlockState(dev_id, BlockIdx);
	if(unwrap->lock_flag) __sync_lock_release(&globalock);
#ifdef DEBUG
  lprintf(lvl, "CacheStartRead(dev_id=%d, BlockIdx=%d) ran succesfully.\n", dev_id, BlockIdx);
#endif
#ifdef STEST
	total_cache_timer[dev_id_idx]+= csecond();
#endif
}

void CacheEndRead(void* wrapped_CacheWrap_p){
	short lvl = 5;
	CacheWrap_p unwrap = (CacheWrap_p) wrapped_CacheWrap_p;
	int BlockIdx = unwrap->BlockIdx;
	short dev_id = unwrap->dev_id, dev_id_idx = idxize(unwrap->dev_id);
#ifdef STEST
	total_cache_timer[dev_id_idx]-= csecond();
#endif
	CacheCheckCorrect(dev_id, BlockIdx);
	if(unwrap->lock_flag) while(__sync_lock_test_and_set (&globalock, 1));

	if(DevCache[dev_id_idx]->BlockReaders[BlockIdx]==0)
			error("CacheEndRead(dev_id=%d, BlockIdx=%d): Already zero readers\n",
				dev_id, BlockIdx);

	DevCache[dev_id_idx]->BlockReaders[BlockIdx]--;
	CacheUpdateBlockState(dev_id, BlockIdx);
	if(unwrap->lock_flag) __sync_lock_release(&globalock);
#ifdef DEBUG
  lprintf(lvl, "CacheEndRead(dev_id=%d, BlockIdx=%d) ran succesfully.\n", dev_id, BlockIdx);
#endif
#ifdef STEST
	total_cache_timer[dev_id_idx]+= csecond();
#endif
}

void CacheStartWrite(void* wrapped_CacheWrap_p){
	short lvl = 5;
	CacheWrap_p unwrap = (CacheWrap_p) wrapped_CacheWrap_p;
	int BlockIdx = unwrap->BlockIdx;
	short dev_id = unwrap->dev_id, dev_id_idx = idxize(unwrap->dev_id);
#ifdef STEST
	total_cache_timer[dev_id_idx]-= csecond();
#endif
	CacheCheckCorrect(dev_id, BlockIdx);
	if(unwrap->lock_flag) while(__sync_lock_test_and_set (&globalock, 1));
	state prev_state = CacheSetBlockState(dev_id, BlockIdx, EXCLUSIVE);
	if (prev_state!= EMPTY && prev_state!= AVAILABLE)
		error("CacheStartWrite(dev_id=%d, BlockIdx=%d) prev_state = %s\n",
			dev_id, BlockIdx, print_state(prev_state));
	if(unwrap->lock_flag) __sync_lock_release(&globalock);
#ifdef DEBUG
  lprintf(lvl, "CacheStartWrite(dev_id=%d, BlockIdx=%d) ran succesfully.\n", dev_id, BlockIdx);
#endif
#ifdef STEST
	total_cache_timer[dev_id_idx]+= csecond();
#endif
}

void CacheEndWrite(void* wrapped_CacheWrap_p){
	short lvl = 5;
	CacheWrap_p unwrap = (CacheWrap_p) wrapped_CacheWrap_p;
	int BlockIdx = unwrap->BlockIdx;
	short dev_id = unwrap->dev_id, dev_id_idx = idxize(unwrap->dev_id);
#ifdef STEST
	total_cache_timer[dev_id_idx]-= csecond();
#endif
	CacheCheckCorrect(dev_id, BlockIdx);
	if(unwrap->lock_flag) while(__sync_lock_test_and_set (&globalock, 1));
	state prev_state = CacheSetBlockState(dev_id, BlockIdx, EXCLUSIVE);
	if (prev_state!= EXCLUSIVE)
		error("CacheEndWrite(dev_id=%d, BlockIdx=%d) prev_state = %s\n",
			dev_id, BlockIdx, print_state(prev_state));
	if(unwrap->lock_flag) __sync_lock_release(&globalock);
#ifdef DEBUG
  lprintf(lvl, "CacheEndWrite(dev_id=%d, BlockIdx=%d) ran succesfully.\n", dev_id, BlockIdx);
#endif
#ifdef STEST
	total_cache_timer[dev_id_idx]+= csecond();
#endif
}

void CacheStartFetch(void* wrapped_CacheWrap_p){
	short lvl = 5;
	CacheWrap_p unwrap = (CacheWrap_p) wrapped_CacheWrap_p;
	int BlockIdx = unwrap->BlockIdx;
	short dev_id = unwrap->dev_id, dev_id_idx = idxize(unwrap->dev_id);
#ifdef STEST
	total_cache_timer[dev_id_idx]-= csecond();
#endif
	CacheCheckCorrect(dev_id, BlockIdx);
	if(unwrap->lock_flag) while(__sync_lock_test_and_set (&globalock, 1));
	state prev_state = CacheSetBlockState(dev_id, BlockIdx, FETCHING);
	if (prev_state!= EMPTY)
		error("CacheStartFetch(dev_id=%d, BlockIdx=%d) prev_state = %s\n",
			dev_id, BlockIdx, print_state(prev_state));
	if(unwrap->lock_flag) __sync_lock_release(&globalock);
#ifdef DEBUG
	lprintf(lvl, "CacheStartFetch(dev_id=%d, BlockIdx=%d) ran succesfully.\n", dev_id, BlockIdx);
#endif
#ifdef STEST
	total_cache_timer[dev_id_idx]+= csecond();
#endif
}

void CacheEndFetchStartRead(void* wrapped_CacheWrap_p){
	short lvl = 5;
	CacheWrap_p unwrap = (CacheWrap_p) wrapped_CacheWrap_p;
	int BlockIdx = unwrap->BlockIdx;
	short dev_id = unwrap->dev_id, dev_id_idx = idxize(unwrap->dev_id);
#ifdef STEST
	total_cache_timer[dev_id_idx]-= csecond();
#endif
	CacheCheckCorrect(dev_id, BlockIdx);
	if(unwrap->lock_flag) while(__sync_lock_test_and_set (&globalock, 1));
	state prev_state = CacheSetBlockState(dev_id, BlockIdx, AVAILABLE);
	if (prev_state!= FETCHING)
		error("CacheEndFetchStartRead(dev_id=%d, BlockIdx=%d) prev_state = %s\n",
			dev_id, BlockIdx, print_state(prev_state));
	DevCache[dev_id_idx]->BlockReaders[BlockIdx]++;
	CacheUpdateBlockState(dev_id, BlockIdx);
	if(unwrap->lock_flag) __sync_lock_release(&globalock);
	free(wrapped_CacheWrap_p);
#ifdef DEBUG
  lprintf(lvl, "CacheEndFetchStartRead(dev_id=%d, BlockIdx=%d) ran succesfully.\n", dev_id, BlockIdx);
#endif
#ifdef STEST
	total_cache_timer[dev_id_idx]+= csecond();
#endif
}

void CacheEndFetchStartWrite(void* wrapped_CacheWrap_p){
	short lvl = 5;
	CacheWrap_p unwrap = (CacheWrap_p) wrapped_CacheWrap_p;
	int BlockIdx = unwrap->BlockIdx;
	short dev_id = unwrap->dev_id, dev_id_idx = idxize(unwrap->dev_id);
#ifdef STEST
	total_cache_timer[dev_id_idx]-= csecond();
#endif
	CacheCheckCorrect(dev_id, BlockIdx);
	if(unwrap->lock_flag) while(__sync_lock_test_and_set (&globalock, 1));
	state prev_state = CacheSetBlockState(dev_id, BlockIdx, AVAILABLE);
	if (prev_state!= FETCHING)
		error("CacheEndFetchStartWrite(dev_id=%d, BlockIdx=%d) prev_state = %s\n",
			dev_id, BlockIdx, print_state(prev_state));
	prev_state = CacheSetBlockState(dev_id, BlockIdx, EXCLUSIVE);
	if(unwrap->lock_flag) __sync_lock_release(&globalock);
	free(wrapped_CacheWrap_p);
#ifdef DEBUG
  lprintf(lvl, "CacheEndFetchStartWrite(dev_id=%d, BlockIdx=%d) ran succesfully.\n", dev_id, BlockIdx);
#endif
#ifdef STEST
	total_cache_timer[dev_id_idx]+= csecond();
#endif
}

long long CoCoGetBlockSize(short dev_id){
	short lvl = 5;
#ifdef DEBUG
	lprintf(lvl-1, "|-----> CoCoGetBlockSize(dev = %d)\n", dev_id);
#endif
	short dev_id_idx = (dev_id == -1) ? LOC_NUM - 1: dev_id;
	if (DevCache[dev_id_idx] == NULL)
		error("CoCoGetBlockSize(%d): Called on empty buffer\n", dev_id);
	return DevCache[dev_id_idx]->BlockSize;
}

void CoCoPeLiaRequestMaxBuffer(short dev_id, long long block_num, long long block_size, long long bufsize_limit){
  short lvl = 3;
#ifdef DEBUG
	lprintf(lvl-1, "|-----> CoCoPeLiaRequestMaxBuffer(dev_id=%d, block_num = %lld, block_size = %lld, bufsize_limit = %lld)\n",
dev_id, block_num, block_size, bufsize_limit);
#endif
#ifdef TEST
	double cpu_timer = csecond();
#endif
  short dev_id_idx = idxize(dev_id);
	DevCachePtr temp_DevCache = CoCoPeLiaDevCacheInit(dev_id, block_num, block_size);
	if (temp_DevCache->mem_buf_sz > 0){
	  long long free_dev_mem, max_dev_mem,  prev_DevCache_sz = 0;
		if (DevCache[dev_id_idx] != NULL) prev_DevCache_sz = DevCache[dev_id_idx]->mem_buf_sz;
		CoCoPeLiaDevGetMemInfo(&free_dev_mem, &max_dev_mem);
	  long long problem_avail_mem = free_dev_mem - max_dev_mem*(1-PROBLEM_GPU_PERCENTAGE/100.0) + prev_DevCache_sz;
	  // For debuging large cases
	  if(MEM_LIMIT>=free_dev_mem)
	  	problem_avail_mem=MEM_LIMIT;
	  else
	  	problem_avail_mem=free_dev_mem;

	  #ifdef DEBUG
	  	lprintf(lvl, "====================================\n");
	  	lprintf(lvl, "Buffer mem management:\n");
	  	lprintf(lvl, " -Buffer requested for BlockSize=%zu MB and BlockNum=%d\n", (size_t) temp_DevCache->BlockSize/1024/1024, temp_DevCache->BlockNum);
	  	lprintf(lvl, " -Mem required for matrices: %zu MB\n", (size_t) temp_DevCache->mem_buf_sz/1024/1024);
	    lprintf(lvl, " -Mem available in loc(%d): %zu MB\n", dev_id, (size_t) problem_avail_mem/1024/1024);
	  #endif
		if (bufsize_limit <= 0){
		  if (temp_DevCache->mem_buf_sz <= problem_avail_mem){
#ifdef DEBUG
		    lprintf(lvl, " -Requested buffer fits in loc(%d)\n", dev_id);
#endif
			;}
			else{
		    temp_DevCache->BlockNum =  (int) (problem_avail_mem/temp_DevCache->BlockSize);
				temp_DevCache->mem_buf_sz = temp_DevCache->BlockNum*temp_DevCache->BlockSize;
#ifdef DEBUG
		    lprintf(lvl, " -Requested buffer does not fit in loc(%d)\n", dev_id);
#endif
			}
	}
	else{
		if(bufsize_limit > problem_avail_mem)
			error("CoCoPeLiaRequestBuffer(dev_id=%d): Requested cache with bufsize_limit =%zu MB bigger than problem_avail_mem = %zu MB\n",
				dev_id, (size_t) bufsize_limit/1024/1024, (size_t) problem_avail_mem/1024/1024);
		else if(bufsize_limit < temp_DevCache->BlockSize)
					error("CoCoPeLiaRequestBuffer(dev_id=%d): Requested cache with bufsize_limit =%zu MB smaller than Blocksize = %zu MB\n",
						dev_id, (size_t) bufsize_limit/1024/1024, (size_t) temp_DevCache->BlockSize/1024/1024);
		else{
			temp_DevCache->BlockNum =  (int) (bufsize_limit/temp_DevCache->BlockSize);
			temp_DevCache->mem_buf_sz = temp_DevCache->BlockNum*temp_DevCache->BlockSize;
		}
	}
#if CACHE_SCHEDULING_POLICY==2 || CACHE_SCHEDULING_POLICY==3 // Memory for hash
	mru_lru_hash[dev_id_idx] = (Node_LL**) malloc(temp_DevCache->BlockNum * sizeof(Node_LL*));
#endif

	#ifdef DEBUG
	  if (prev_DevCache_sz >= temp_DevCache->mem_buf_sz)
			lprintf(lvl, " -Loc(%d) buf available: %zu MB\n", dev_id, (size_t) prev_DevCache_sz/1024/1024);
	  else if (prev_DevCache_sz > 0) lprintf(lvl, " -Smaller Loc(%d) buf available -> replacing : %zu -> %zu MB\n",
			dev_id, (size_t) prev_DevCache_sz/1024/1024, (size_t) temp_DevCache->mem_buf_sz/1024/1024);
	  else lprintf(lvl, " -Initializing new Loc(%d) buffer: %zu MB\n", dev_id, (size_t) temp_DevCache->mem_buf_sz/1024/1024);
	#endif
	  if (prev_DevCache_sz >= temp_DevCache->mem_buf_sz){
			temp_DevCache->mem_buf_sz = DevCache[dev_id_idx]->mem_buf_sz;
			temp_DevCache->gpu_mem_buf = DevCache[dev_id_idx]->gpu_mem_buf;
			free(DevCache[dev_id_idx]->BlockState);
			free(DevCache[dev_id_idx]->BlockReaders);
			free(DevCache[dev_id_idx]->BlockCurrentTileDim);
			free(DevCache[dev_id_idx]->BlockCurrentTilePtr);
			free(DevCache[dev_id_idx]);
			DevCache[dev_id_idx] = temp_DevCache;
		}
	  else{
			if (prev_DevCache_sz > 0){
			  CoCoFree(DevCache[dev_id_idx]->gpu_mem_buf, dev_id);
				free(DevCache[dev_id_idx]->BlockState);
				free(DevCache[dev_id_idx]->BlockReaders);
				free(DevCache[dev_id_idx]->BlockCurrentTileDim);
				free(DevCache[dev_id_idx]->BlockCurrentTilePtr);
				free(DevCache[dev_id_idx]);
			}
	    //DevCache[dev_id_idx] = temp_DevCache;
			//DevCache[dev_id_idx]->gpu_mem_buf = CoCoMalloc(DevCache[dev_id_idx]->mem_buf_sz,
			//	dev_id);
			temp_DevCache->gpu_mem_buf = CoCoMalloc(temp_DevCache->mem_buf_sz, dev_id);
			DevCache[dev_id_idx] = temp_DevCache;
	  }
	  CoCoSyncCheckErr();

#ifdef TEST
		cpu_timer = csecond() - cpu_timer;
		lprintf(lvl, "GPU(%d) Buffer allocation with sz = %zu MB: t_alloc = %lf ms\n" ,
	    dev_id, (size_t) DevCache[dev_id_idx]->mem_buf_sz/1024/1024, cpu_timer*1000);
#endif

#ifdef DEBUG
		lprintf(lvl, "GPU(%d) Buffer allocation (Size = %zu MB, Blocksize = %zu MB, BlockNum = %d)\n" ,
	  dev_id, (size_t) DevCache[dev_id_idx]->mem_buf_sz/1024/1024,
	  (size_t) DevCache[dev_id_idx]->BlockSize/1024/1024,  DevCache[dev_id_idx]->BlockNum);
		lprintf(lvl-1, "<-----|\n");
#endif
	}
	else{;
#ifdef DEBUG
		lprintf(lvl, "GPU(%d) does not require a buffer - all data available\n" , dev_id);
		lprintf(lvl-1, "<-----|\n");
#endif
	}
}


const char *printlist(state *list, int length){
	char* outstring = (char*) malloc(length*64*sizeof(char));
  sprintf(outstring, "[");
  for (int i =0; i < length; i++) sprintf(outstring + strlen(outstring), " %s", print_state(list[i]));
	sprintf(outstring + strlen(outstring), " ]");
  return outstring;
}

void CachePrint(short dev_id){
	short dev_id_idx = idxize(dev_id);
	if (DevCache[dev_id_idx] == NULL)
		error("CachePrint(%d): Called on empty buffer\n", dev_id);
	lprintf(0, "Cache(dev_id=%d):\n%s\n%s\n", dev_id,
		printlist(DevCache[dev_id_idx]->BlockReaders, DevCache[dev_id_idx]->BlockNum),
		printlist(DevCache[dev_id_idx]->BlockState, DevCache[dev_id_idx]->BlockNum));

}

#ifdef STEST
double CacheGetTimer(short dev_id){
	return total_cache_timer[idxize(dev_id)];
}
#endif
