
# perf count 
https://claude.ai/chat/594f86b6-a891-43a8-b99a-a61b3dca14e9

// Activation du DWT
CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
DWT->CYCCNT = 0;
DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

// Mesure
uint32_t start = DWT->CYCCNT;
// ... code à mesurer ...
uint32_t cycles = DWT->CYCCNT - start;

# Accès direct aux registres (dépend de l'implémentation)
DWT_CTRL = 0xE0001000
DWT_CYCCNT = 0xE0001004

--
MODELS
  . ring with mask 
    + data buffer seen as contiguous (2 half ring)
    + simple
    - need to mask for each sample access
    
  . two separate half rings 
    + no need to mask
    - need to copy LOOK_AHEAD data
    - complex

  going with "ring with mask" first

Type of read

### full / partial : got data for a full buffer

  may be partial if
    - end of loop and looping
    - end of sample
  
  on partial
    end of loop (is looping implied)
      * copy loop start in other ring, plan for two fills
      * copy LOOK_AHEAD from loop start (what if ) 
      
    end of sample
      zero_fill up to LOOK_AHEAD
      mark voice as done
      
  
  
  
  ## loop miseras
  * 
  

  [________LLLL....] [_____________]
    
    
  
### single HR : can read from a single buffer (incuding LOOK_AHEAD)

  * single HR read -> check that other HR is prefetching, or start prefetch
