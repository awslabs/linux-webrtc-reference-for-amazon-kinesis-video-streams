#!/bin/bash

set -e

SCRIPT_DIR=$(dirname "$(readlink -f "$0")")
echo SCRIPT_DIR: "${SCRIPT_DIR}"

export AWS_DEFAULT_REGION="${AWS_REGION:-eu-central-1}"

# Generate unique identifier 
UNIQUE_ID="${UNIQUE_ID:-`date +%s`}"
ROLE_NAME="KVSWebRTCRole_${UNIQUE_ID}"
POLICY_NAME_BASE="KVSWebRTCPolicy_${UNIQUE_ID}"


echo "Creating with UNIQUE_ID: ${UNIQUE_ID}"
echo "Role name: ${ROLE_NAME}"
echo "Policy name: ${POLICY_NAME_BASE}"

# Create trust policy with enhanced security
cat << EOF > ${SCRIPT_DIR}/trust-policy.json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Principal": {
        "Service": "credentials.iot.amazonaws.com"
      },
      "Action": "sts:AssumeRole"
    }
  ]
}
EOF

echo "Create the role policy document with enhanced permissions"
cat << EOF > ${SCRIPT_DIR}/role-policy.json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Action": [
        "kinesisvideo:*",
        "iot:Connect",
        "iot:Publish",
        "iot:Subscribe",
        "iot:Receive",
        "iot:AssumeRoleWithCertificate",
        "iot:DescribeEndpoint"
      ],
      "Resource": "*"
    }
  ]
}
EOF

echo "Create role alias policy"
cat << EOF > ${SCRIPT_DIR}/role-alias-policy.json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Action": [
        "iot:CreateRoleAlias",
        "iot:DescribeRoleAlias",
        "iot:UpdateRoleAlias",
        "iot:DeleteRoleAlias"
      ],
      "Resource": "arn:aws:iot:*:*:rolealias/*"
    }
  ]
}
EOF

echo "Create the IAM role"
aws iam create-role \
    --role-name "${ROLE_NAME}" \
    --assume-role-policy-document file://${SCRIPT_DIR}/trust-policy.json

echo "Attach the policies to the role"
aws iam put-role-policy \
    --role-name "${ROLE_NAME}" \
    --policy-name "${POLICY_NAME_BASE}" \
    --policy-document file://${SCRIPT_DIR}/role-policy.json

aws iam put-role-policy \
    --role-name "${ROLE_NAME}" \
    --policy-name "IoTRoleAliasPolicy" \
    --policy-document file://${SCRIPT_DIR}/role-alias-policy.json

echo "Get the role ARN"
AWS_ROLE_ARN_TO_ASSUME=$(aws iam get-role --role-name "${ROLE_NAME}" --query 'Role.Arn' --output text)
echo "Role ARN: ${AWS_ROLE_ARN_TO_ASSUME}"

echo "Generate certificate and save output"
aws iot create-keys-and-certificate --set-as-active \
    --certificate-pem-outfile "${SCRIPT_DIR}/../../certificate.pem" \
    --public-key-outfile "${SCRIPT_DIR}/../../public.key" \
    --private-key-outfile "${SCRIPT_DIR}/../../private.key" \
    > ${SCRIPT_DIR}/create-keys-and-certificate.json

echo "Extract certificate details"
CERTIFICATE_ARN=$(jq -r '.certificateArn' ${SCRIPT_DIR}/create-keys-and-certificate.json)
echo "CERTIFICATE_ARN: ${CERTIFICATE_ARN}"

echo "Create IoT Thing with specific name"
THING_NAME="KVSWebRTCDevice_${UNIQUE_ID}"
aws iot create-thing --thing-name "${THING_NAME}" > ${SCRIPT_DIR}/thing_output.json
THING_ARN=$(cat ${SCRIPT_DIR}/thing_output.json | jq -r '.thingArn')

# Create and attach IoT policy with specific name
IOT_POLICY_NAME="KVSWebRTCDevicePolicy_${UNIQUE_ID}"
cat << EOF > ${SCRIPT_DIR}/iot-policy.json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Action": [
        "iot:Connect",
        "iot:DescribeCredentials",
        "iot:AssumeRoleWithCertificate"
      ],
      "Resource": "*"
    }
  ]
}
EOF

aws iot create-policy --policy-name "${IOT_POLICY_NAME}" --policy-document file://${SCRIPT_DIR}/iot-policy.json

echo "Attach policy to certificate"
aws iot attach-policy --policy-name "${IOT_POLICY_NAME}" --target "${CERTIFICATE_ARN}"

echo "Attach certificate to thing"
aws iot attach-thing-principal --thing-name "${THING_NAME}" --principal "${CERTIFICATE_ARN}"

echo "Create role alias with specific name"
ROLE_ALIAS="KVSWebRTCRole_${UNIQUE_ID}"
aws iot create-role-alias \
  --credential-duration-seconds 3600 \
  --role-alias "${ROLE_ALIAS}" \
  --role-arn "${AWS_ROLE_ARN_TO_ASSUME}"

echo "Save credentials endpoint"
CREDENTIALS_ENDPOINT="$(aws --output text iot describe-endpoint --endpoint-type iot:CredentialProvider --query 'endpointAddress')"

echo "Create KVS channel"
CHANNEL_NAME="KVSWebRTCChannel_${UNIQUE_ID}"
echo "Creating KVS channel: ${CHANNEL_NAME}"
aws kinesisvideo create-signaling-channel \
    --channel-name "${CHANNEL_NAME}" \
    --channel-type SINGLE_MASTER


echo "Export variables for next steps"
echo ${CHANNEL_NAME} > ${SCRIPT_DIR}/CHANNEL_NAME
echo ${CREDENTIALS_ENDPOINT} > ${SCRIPT_DIR}/CREDENTIALS_ENDPOINT
echo ${IOT_POLICY_NAME} > ${SCRIPT_DIR}/IOT_POLICY_NAME
echo ${ROLE_ALIAS} > ${SCRIPT_DIR}/ROLE_ALIAS
echo ${THING_NAME} > ${SCRIPT_DIR}/THING_NAME
echo ${UNIQUE_ID} > ${SCRIPT_DIR}/UNIQUE_ID
echo ${AWS_DEFAULT_REGION} > ${SCRIPT_DIR}/AWS_REGION

echo "Setup completed successfully!"

if [ -n "$GITHUB_ENV" ]; then
  echo "Storing for GITHUB_ENV"
  echo "THING_NAME=${THING_NAME}" >> $GITHUB_ENV
  echo "ROLE_ALIAS=${ROLE_ALIAS}" >> $GITHUB_ENV
  echo "CREDENTIALS_ENDPOINT=${CREDENTIALS_ENDPOINT}" >> $GITHUB_ENV
  echo "POLICY_NAME=${POLICY_NAME}" >> $GITHUB_ENV
  echo "CHANNEL_NAME=${CHANNEL_NAME}" >> $GITHUB_ENV
fi
