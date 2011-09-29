#!/bin/bash

ACTUAL=$( pwd )

LLAVE="Icedove_____________________Guacharo"

OPCIONES="${LLAVE} $( echo ${LLAVE} | tr '[:lower:]' '[:upper:]' ) $( echo ${LLAVE} | tr '[:upper:]' '[:lower:]' )"

for REEMPLAZAR in ${OPCIONES}; do

	ICEDOVE=${REEMPLAZAR%_____________________*}
	GUACHARO=${REEMPLAZAR#${ICEDOVE}_____________________}

	for ARCHIVO in $( find . -type d | grep -v ".git/" | grep -v "convertir-a-guacharo.sh" ); do
		NUEVO=$( echo ${ARCHIVO} | sed "s/${ICEDOVE}/${GUACHARO}/g" )
		if [ "${ARCHIVO}" != "${NUEVO}" ]; then
			echo "Renombrando ${ARCHIVO} a ${NUEVO}"
			mv ${ARCHIVO} ${NUEVO}
		fi
	done

	for ARCHIVO in $( find . -type f | grep -v ".git/" | grep -v "convertir-a-guacharo.sh" ); do
		NUEVO=$( echo ${ARCHIVO} | sed "s/${ICEDOVE}/${GUACHARO}/g" )
		if [ "${ARCHIVO}" != "${NUEVO}" ]; then
			echo "Renombrando ${ARCHIVO} a ${NUEVO}"
			mv ${ARCHIVO} ${NUEVO}
		fi
	done

	for ARCHIVO in $( grep -IHR "${ICEDOVE}" . | cut -d: -f1 | grep -v ".git/" | grep -v "convertir-a-guacharo.sh" ); do
		echo "Sustituyendo ${ICEDOVE} por ${GUACHARO} en ${ARCHIVO}"
		sed -i "s/${ICEDOVE}/${GUACHARO}/g" ${ARCHIVO}
	done
done



