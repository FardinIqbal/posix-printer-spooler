#include <criterion/criterion.h>
#include <criterion/logging.h>
#include <stdio.h>
#include <sys/time.h>

#include "driver.h"
#include "__helper.h"

#define QUOTE1(x) #x
#define QUOTE(x) QUOTE1(x)
#define SCRIPT1(x) x##_script
#define SCRIPT(x) SCRIPT1(x)

#define SUITE printer_suite

/*---------------------------test first available printer----------------------------*/
/* Create three printers which are all eligible for printing the file. Only enable one
   their code should print upon that printer without a problem
*/
#define TEST_NAME print_first_available
#define type_cmd    "type aaa"
#define print_cmd   "print test_scripts/testfile.aaa"
#define printer1    "printer Alice1 aaa"
#define printer2    "printer Alice2 aaa"
#define printer3    "printer Alice3 aaa"
#define enable_cmd  "enable Alice2"
#define quit_cmd    "quit"
static COMMAND SCRIPT(TEST_NAME)[] = {
    // send,                expect,                     modifiers,            timeout,  before,    after
    {  NULL,                INIT_EVENT,                 0,                    HND_MSEC,   NULL,      NULL },
    {  type_cmd,            TYPE_DEFINED_EVENT,         0,                    HND_MSEC,   NULL,      NULL },
    {  print_cmd,           JOB_CREATED_EVENT,          EXPECT_SKIP_OTHER,    HND_MSEC,   NULL,      NULL },
    {  print_cmd,           JOB_CREATED_EVENT,          EXPECT_SKIP_OTHER,    HND_MSEC,   NULL,      NULL },
    {  print_cmd,           JOB_CREATED_EVENT,          EXPECT_SKIP_OTHER,    HND_MSEC,   NULL,      NULL },
    {  printer1,            PRINTER_DEFINED_EVENT,      EXPECT_SKIP_OTHER,    HND_MSEC,   NULL,      NULL },
    {  printer2,            PRINTER_DEFINED_EVENT,      EXPECT_SKIP_OTHER,    HND_MSEC,   NULL,      NULL },
    {  printer3,            PRINTER_DEFINED_EVENT,      EXPECT_SKIP_OTHER,    HND_MSEC,   NULL,      NULL },    
    {  enable_cmd,          JOB_FINISHED_EVENT,         EXPECT_SKIP_OTHER,    ZERO_SEC,   NULL,      NULL },
    {  NULL,                JOB_FINISHED_EVENT,         EXPECT_SKIP_OTHER,    ZERO_SEC,   NULL,      NULL },
    {  NULL,                JOB_FINISHED_EVENT,         EXPECT_SKIP_OTHER,    ZERO_SEC,   NULL,      NULL },
    {  quit_cmd,            FINI_EVENT,                 EXPECT_SKIP_OTHER,    HND_MSEC,   NULL,      NULL },
    {  NULL,                EOF_EVENT,                  0,                    TEN_MSEC,   NULL,      NULL }
};

Test(SUITE, TEST_NAME, .init = test_delay_setup, .fini = test_teardown, .timeout = 10)
{
    int err, status;
    char *name = QUOTE(SUITE)"/"QUOTE(TEST_NAME);
    char *argv[] = {TEST_EXECUTABLE, NULL};
    err = run_test(name, argv[0], argv, SCRIPT(TEST_NAME), &status);
    assert_proper_exit_status(err, status);
}
#undef type_cmd   
#undef print_cmd  
#undef printer1   
#undef printer2   
#undef printer3   
#undef enable_cmd 
#undef TEST_NAME

