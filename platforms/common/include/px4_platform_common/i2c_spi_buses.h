/****************************************************************************
 *
 *   Copyright (c) 2020 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

#pragma once

#include "i2c.h"
#include "spi.h"

#include <stdint.h>

#include <lib/conversion/rotation.h>
#include <px4_platform_common/px4_work_queue/ScheduledWorkItem.hpp>
#include <px4_platform_common/atomic.h>
#include <board_config.h>

enum class I2CSPIBusOption : uint8_t {
	All = 0, ///< select all runnning instances
	I2CInternal,
	I2CExternal,
	SPIInternal,
	SPIExternal,
};

/**
 * @class I2CSPIInstance
 * I2C/SPI driver instance used by BusInstanceIterator to find running instances.
 */
class I2CSPIInstance
{
public:
	I2CSPIInstance(I2CSPIBusOption bus_option, int bus)
		: _bus_option(bus_option), _bus(bus) {}

	virtual ~I2CSPIInstance() = default;

private:
	friend class BusInstanceIterator;
	friend class I2CSPIDriverNoTemplate;

	const I2CSPIBusOption _bus_option;
	const int _bus;
};

struct BusCLIArguments {
	I2CSPIBusOption bus_option{I2CSPIBusOption::All};
	int requested_bus{-1};
	int chipselect_index{1};
	Rotation rotation{ROTATION_NONE};
	int custom1; ///< driver-specific custom argument
	int custom2; ///< driver-specific custom argument
};

/**
 * @class BusInstanceIterator
 * Iterate over running instances and/or configured I2C/SPI buses with given filter options.
 */
class BusInstanceIterator
{
public:
	BusInstanceIterator(I2CSPIInstance **instances, int max_num_instances,
			    const BusCLIArguments &cli_arguments, uint16_t devid_driver_index);
	~BusInstanceIterator() = default;

	I2CSPIBusOption configuredBusOption() const { return _bus_option; }

	int nextFreeInstance() const;

	bool next();

	I2CSPIInstance *instance() const;
	void resetInstance();
	board_bus_types busType() const;
	int bus() const;
	uint32_t devid() const;
	bool external() const;

	static I2CBusIterator::FilterType i2cFilter(I2CSPIBusOption bus_option);
	static SPIBusIterator::FilterType spiFilter(I2CSPIBusOption bus_option);
private:
	I2CSPIInstance **_instances;
	const int _max_num_instances;
	const I2CSPIBusOption _bus_option;
	SPIBusIterator _spi_bus_iterator;
	I2CBusIterator _i2c_bus_iterator;
	int _current_instance{-1};
};

/**
 * @class I2CSPIDriverNoTemplate
 * Base class for I2C/SPI driver modules (non-templated, used by I2CSPIDriver)
 */
class I2CSPIDriverNoTemplate : public px4::ScheduledWorkItem, public I2CSPIInstance
{
public:
	I2CSPIDriverNoTemplate(const char *module_name, const px4::wq_config_t &config, I2CSPIBusOption bus_option, int bus)
		: ScheduledWorkItem(module_name, config),
		  I2CSPIInstance(bus_option, bus) {}

	static int module_stop(BusInstanceIterator &iterator);
	static int module_status(BusInstanceIterator &iterator);
	static int module_custom_method(const BusCLIArguments &cli, BusInstanceIterator &iterator);
protected:
	virtual ~I2CSPIDriverNoTemplate() = default;

	virtual void print_status();

	virtual void custom_method(const BusCLIArguments &cli) {}

	void exit_and_cleanup() { ScheduleClear(); _task_exited.store(true); }
	bool should_exit() const { return _task_should_exit.load(); }

	using instantiate_method = I2CSPIInstance * (*)(const BusCLIArguments &cli, const BusInstanceIterator &iterator,
				   int runtime_instance);
	static int module_start(const BusCLIArguments &cli, BusInstanceIterator &iterator, void(*print_usage)(),
				instantiate_method instantiate, I2CSPIInstance **instances);

private:
	void request_stop_and_wait();

	px4::atomic_bool _task_should_exit{false};
	px4::atomic_bool _task_exited{false};
};

/**
 * @class I2CSPIDriver
 * Base class for I2C/SPI driver modules
 */
template<class T, int MAX_NUM = 3>
class I2CSPIDriver : public I2CSPIDriverNoTemplate
{
public:
	static constexpr int max_num_instances = MAX_NUM;

	static int module_start(const BusCLIArguments &cli, BusInstanceIterator &iterator)
	{
		return I2CSPIDriverNoTemplate::module_start(cli, iterator, &T::print_usage, &T::instantiate, _instances);
	}

	static I2CSPIInstance **instances() { return _instances; }

protected:
	I2CSPIDriver(const char *module_name, const px4::wq_config_t &config, I2CSPIBusOption bus_option, int bus)
		: I2CSPIDriverNoTemplate(module_name, config, bus_option, bus) {}

	virtual ~I2CSPIDriver() = default;

	void Run() final {
		static_cast<T *>(this)->RunImpl();

		if (should_exit())
		{
			exit_and_cleanup();
		}
	}
private:
	static I2CSPIInstance *_instances[MAX_NUM];
};

template<class T, int MAX_NUM>
I2CSPIInstance *I2CSPIDriver<T, MAX_NUM>::_instances[MAX_NUM] {};
