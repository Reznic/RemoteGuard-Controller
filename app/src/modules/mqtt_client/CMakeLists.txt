#
# Copyright (c) 2023 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/mqtt_client.c)

if (CONFIG_MQTT_CLIENT_PROVISION_CERTIFICATES)
	message(WARNING "Credentials are exposed in non-secure memory. This should be avoided in production.")

	set(app_certs_binary_dir ${APPLICATION_BINARY_DIR}/certs)
	zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/cert)

	function(process_certificate definition_name file_name)
		set(cert_file ${APPLICATION_SOURCE_DIR}/${CONFIG_MQTT_CLIENT_CERTIFICATES_FOLDER}/${file_name})
		if(EXISTS ${cert_file})
			message(STATUS "${file_name} found")

			get_filename_component(file_base_name ${file_name} NAME_WE)
			set(inc_file_name ${file_base_name}.inc)

			set(inc_file_path ${app_certs_binary_dir}/${inc_file_name})
			generate_inc_file_for_target(app ${cert_file} ${inc_file_path})

			target_compile_definitions(app PRIVATE "${definition_name}=\"${inc_file_path}\"")
		endif()
	endfunction()

	process_certificate("MQTT_CLIENT_CA_CERT" "ca-cert.pem")
	process_certificate("MQTT_CLIENT_CLIENT_CERT" "client-cert.pem")
	process_certificate("MQTT_CLIENT_PRIVATE_KEY" "private-key.pem")
	process_certificate("MQTT_CLIENT_CA_CERT_2" "ca-cert-2.pem")
	process_certificate("MQTT_CLIENT_CLIENT_CERT_2" "client-cert-2.pem")
	process_certificate("MQTT_CLIENT_PRIVATE_KEY_2" "private-key-2.pem")

endif()
