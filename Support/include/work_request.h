//
//  work_request.h.swift
//  OSpost
//
//  Created by Cosas on 12/22/25.
//

#pragma once
#include <stdint.h>

typedef enum {
  INTENT_INTERACTIVE = 0,
  INTENT_AMBIENT     = 1,
  INTENT_SERVICE     = 2,
  INTENT_BATCH       = 3,
  INTENT_ML          = 4,
} work_intent_t;

typedef enum { LATENCY_LOW, LATENCY_MED, LATENCY_HIGH } latency_class_t;
typedef enum { THROUGHPUT_LOW, THROUGHPUT_MED, THROUGHPUT_HIGH } throughput_class_t;

typedef struct {
  work_intent_t intent;
  latency_class_t latency;
  throughput_class_t throughput;
  uint32_t energy_hint_mw;   // stub: "budget hint", not enforced yet
} work_request_t;
