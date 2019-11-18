// Include the most common headers from the C standard library
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

// Include the main libnx system header, for Switch development
#include <switch.h>

// See also libnx nfc.h.

// Indefinitely wait for an event to be signaled
// Break when + is pressed, or if the application should quit (in this case, return value will be non-zero)
Result eventWaitLoop(Event *event) {
    Result rc = 1;
    while (appletMainLoop()) {
        rc = eventWait(event, 0);
        hidScanInput();
        if (R_SUCCEEDED(rc) || (hidKeysDown(CONTROLLER_P1_AUTO) & KEY_PLUS))
            break;
        consoleUpdate(NULL);
    }
    return rc;
}

// Print raw data as hexadecimal numbers.
void print_hex(void *buf, size_t size) {
    for (size_t i=0; i<size; i++)
        printf("%02X", ((u8*)buf)[i]);
    printf("\n");
    consoleUpdate(NULL);
}

Result process_amiibo(u32 app_id) {
    Result rc = 0;

    // Get the handle of the first controller with NFC capabilities.
    NfcDeviceHandle handle={0};
    if (R_SUCCEEDED(rc)) {
        s32 device_count;
        rc = nfpListDevices(&device_count, &handle, 1);

        if (R_FAILED(rc))
            return rc;
    }

    // Get the activation event. This is signaled when a tag is detected.
    Event activate_event = {0};
    if (R_FAILED(nfpAttachActivateEvent(&handle, &activate_event)))
        goto fail_0;

    // Get the deactivation event. This is signaled when a tag is removed.
    Event deactivate_event = {0};
    if (R_FAILED(nfpAttachDeactivateEvent(&handle, &deactivate_event)))
        goto fail_1;

    NfpState state = 0;
    if (R_SUCCEEDED(rc)) {
        rc = nfpGetState(&state);

        if (R_SUCCEEDED(rc) && state == NfpState_NonInitialized) {
            printf("Bad nfp state: %u\n", state);
            consoleUpdate(NULL);
            rc = -1;
        }
    }

    NfpDeviceState device_state = 0;
    if (R_SUCCEEDED(rc)) {
        rc = nfpGetDeviceState(&handle, &device_state);

        if (R_SUCCEEDED(rc) && device_state > NfpDeviceState_TagFound) {
            printf("Bad nfp device state: %u\n", device_state);
            consoleUpdate(NULL);
            rc = -1;
        }
    }

    if (R_FAILED(rc))
        goto fail_1;

    // Start the detection of tags.
    rc = nfpStartDetection(&handle);
    if (R_SUCCEEDED(rc)) {
        printf("Scanning for a tag...\n");
        consoleUpdate(NULL);
    }

    // Wait until a tag is detected.
    // You could also wait until nfpGetDeviceState returns NfpDeviceState_TagFound.
    if (R_SUCCEEDED(rc)) {
        rc = eventWaitLoop(&activate_event);

        if (R_SUCCEEDED(rc)) {
            printf("A tag was detected, please do not remove it from the NFC spot.\n");
            consoleUpdate(NULL);
        }
    }

    // If a tag was successfully detected, load it into memory.
    if (R_SUCCEEDED(rc))
        rc = nfpMount(&handle, NfpDeviceType_Amiibo, NfpMountTarget_All);

    // Retrieve the model info data, which contains the amiibo id.
    if (R_SUCCEEDED(rc)) {
        NfpModelInfo model_info = {0};
        rc = nfpGetModelInfo(&handle, &model_info);

        if (R_SUCCEEDED(rc)) {
            printf("Amiibo ID: ");
            print_hex(model_info.amiibo_id, 8);
        }
    }

    // Retrieve the common info data, which contains the application area size.
    u32 app_area_size = 0;
    if (R_SUCCEEDED(rc)) {
        NfpCommonInfo common_info = {0};
        rc = nfpGetCommonInfo(&handle, &common_info);

        if (R_SUCCEEDED(rc))
            app_area_size = common_info.application_area_size;
    }

    u32 npad_id = 0;
    if (R_SUCCEEDED(rc)) {
        rc = nfpOpenApplicationArea(&handle, app_id, &npad_id);

        if (rc == 0x10073) // 2115-0128
            printf("This tag contains no application data.\n");
        if (rc == 0x13073) // 2115-0152
            printf("This tag contains application data associated with an ID other than 0x%x.\n", app_id);
    }

    u8 app_area[0xd8] = {0}; // Maximum size of the application area.
    if (app_area_size > sizeof(app_area)) app_area_size = sizeof(app_area);
    if (R_SUCCEEDED(rc)) {
        rc = nfpGetApplicationArea(&handle, app_area, app_area_size);

        if (R_SUCCEEDED(rc)) {
            printf("App area:\n");
            print_hex(app_area, app_area_size);
        }
    }

    // Wait until the tag is removed.
    // You could also wait until nfpGetDeviceState returns NfpDeviceState_TagRemoved.
    if (R_SUCCEEDED(rc)) {
        printf("You can now remove the tag.\n");
        consoleUpdate(NULL);
        eventWaitLoop(&deactivate_event);
    }

    // Unmount the tag.
    nfpUnmount(&handle);

    // Stop the detection of tags.
    nfpStopDetection(&handle);

    // Cleanup.
fail_1:
    eventClose(&deactivate_event);
fail_0:
    eventClose(&activate_event);

    return rc;
}

// Main program entrypoint
int main(int argc, char* argv[])
{
    Result rc = 0;

    // Hardcoded for Super Smash Bros. Ultimate.
    // See also: https://switchbrew.org/wiki/NFC_services#Application_IDs
    u32 app_id = 0x34f80200;

    // This example uses a text console, as a simple way to output text to the screen.
    // If you want to write a software-rendered graphics application,
    //   take a look at the graphics/simplegfx example, which uses the libnx Framebuffer API instead.
    // If on the other hand you want to write an OpenGL based application,
    //   take a look at the graphics/opengl set of examples, which uses EGL instead.
    consoleInit(NULL);

    printf("NFC example program.\n");
    printf("Scan an amiibo tag to display information about it.\n\n");
    consoleUpdate(NULL);

    // Initialize the nfp:* service.
    rc = nfpInitialize(NfpServiceType_User);

    // Check if NFC is enabled. If not, wait until it is.
    // Note that various official games don't use nfc*().
    if (R_SUCCEEDED(rc)) rc = nfcInitialize(NfcServiceType_User);
    if (R_SUCCEEDED(rc)) {
        bool nfc_enabled = false;
        rc = nfcIsNfcEnabled(&nfc_enabled);

        if (R_SUCCEEDED(rc) && !nfc_enabled) {
            // Get the availability change event. This is signaled when a change in NFC availability happens. See libnx nfc.h for the required sysver.
            Event availability_change_event = {0};
            rc = nfpAttachAvailabilityChangeEvent(&availability_change_event);

            // Wait for a change in availability.
            if (R_SUCCEEDED(rc)) {
                printf("NFC is disabled. Please turn off plane mode via the quick settings to continue.\n");
                consoleUpdate(NULL);
                rc = eventWaitLoop(&availability_change_event);
            }

            eventClose(&availability_change_event);
        }

        nfcExit();
    }

    if (R_FAILED(rc))
        goto fail_main;

    printf("Press A to process an amiibo.\n");
    printf("Press + at any time to exit.\n");
    printf("Waiting for user input...\n\n");
    consoleUpdate(NULL);

    // Main loop
    while (appletMainLoop()) {
        // Scan all the inputs. This should be done once for each frame
        hidScanInput();

        // hidKeysDown returns information about which buttons have been
        // just pressed in this frame compared to the previous one
        if (hidKeysDown(CONTROLLER_P1_AUTO) & KEY_A) {
            rc = process_amiibo(app_id);

            // If an error happened, print it.
            if (R_FAILED(rc))
                printf("Error: 0x%x.\n", rc);

            printf("Waiting for user input...\n\n");
        }

        // If + was pressed to exit an eventWaitLoop(), we also catch it here.
        if (hidKeysDown(CONTROLLER_P1_AUTO) & KEY_PLUS)
            break; // break in order to return to hbmenu

        // Update the console, sending a new frame to the display
        consoleUpdate(NULL);
    }

fail_main:
    nfpExit();

    // Deinitialize and clean up resources used by the console (important!)
    consoleExit(NULL);

    return 0;
}
