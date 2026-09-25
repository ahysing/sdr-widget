################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/loudness_internal.c \
../src/loudness_first_order.c \
../src/usb_stats_hid_report_descriptor.c \
../src/stats_telemetry.c \
../src/usb_fifo_hw_lock.c \
../src/usb_statistics_descriptors.c \
../src/usb_statistics.c \
../src/loudness_inferred_gain.c \
../src/loudness_fast.c \
../src/loudness.c \
../src/I2C.c \
../src/Mobo_config.c \
../src/composite_widget.c \
../src/device_audio_task.c \
../src/device_mouse_hid_task.c \
../src/features.c \
../src/image.c \
../src/taskAK5394A.c \
../src/taskMoboCtrl.c \
../src/uac2_device_audio_task.c \
../src/uac2_image.c \
../src/uac2_taskAK5394A.c \
../src/uac2_usb_descriptors.c \
../src/uac2_usb_specific_request.c \
../src/usb_descriptors.c \
../src/usb_specific_request.c \
../src/pcm5142.c \
../src/wm8804.c 


OBJS += \
./src/loudness_internal.o \
./src/loudness_first_order.o \
./src/usb_stats_hid_report_descriptor.o \
./src/stats_telemetry.o \
./src/usb_fifo_hw_lock.o \
./src/usb_statistics_descriptors.o \
./src/usb_statistics.o \
./src/loudness_inferred_gain.o \
./src/loudness_fast.o \
./src/loudness.o \
./src/I2C.o \
./src/Mobo_config.o \
./src/composite_widget.o \
./src/device_audio_task.o \
./src/device_mouse_hid_task.o \
./src/features.o \
./src/image.o \
./src/taskAK5394A.o \
./src/taskMoboCtrl.o \
./src/uac2_device_audio_task.o \
./src/uac2_image.o \
./src/uac2_taskAK5394A.o \
./src/uac2_usb_descriptors.o \
./src/uac2_usb_specific_request.o \
./src/usb_descriptors.o \
./src/usb_specific_request.o \
./src/pcm5142.o \
./src/wm8804.o 



C_DEPS += \
./src/loudness_internal.d \
./src/loudness_first_order.d \
./src/usb_stats_hid_report_descriptor.d \
./src/stats_telemetry.d \
./src/usb_fifo_hw_lock.d \
./src/usb_statistics_descriptors.d \
./src/usb_statistics.d \
./src/loudness_inferred_gain.d \
./src/loudness_fast.d \
./src/loudness.d \
./src/I2C.d \
./src/Mobo_config.d \
./src/composite_widget.d \
./src/device_audio_task.d \
./src/device_mouse_hid_task.d \
./src/features.d \
./src/image.d \
./src/taskAK5394A.d \
./src/taskMoboCtrl.d \
./src/uac2_device_audio_task.d \
./src/uac2_image.d \
./src/uac2_taskAK5394A.d \
./src/uac2_usb_descriptors.d \
./src/uac2_usb_specific_request.d \
./src/usb_descriptors.d \
./src/usb_specific_request.d \
./src/pcm5142.d \
./src/wm8804.d



# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.c
	@echo Compile $(CFLAGS) $<
	@avr32-gcc $(CFLAGS) $(AVR32_APP_INCLUDES) -std=gnu99 -fgnu89-inline -O2 -fdata-sections -Wall -c -fmessage-length=0 -ffunction-sections -masm-addr-pseudos -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o"$@" "$<"


