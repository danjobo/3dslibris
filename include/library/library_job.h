#pragma once

class Book;

enum app_job_type_t
{
  APP_JOB_INDEX_METADATA,
  APP_JOB_EXTRACT_COVER,
  APP_JOB_EXTRACT_LARGE_COVER,
  APP_JOB_RESOLVE_TOC
};

struct app_job_t
{
  app_job_type_t type;
  Book *book;
};
