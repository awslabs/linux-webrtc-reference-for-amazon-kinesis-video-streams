#!/bin/bash

SCRIPT_DIR=$(dirname "$(readlink -f "$0")")

# taken from environment if created by generate-iot-credentials, or set them here manually
export AWS_DEFAULT_REGION="${AWS_REGION:-eu-central-1}"

read CHANNEL_NAME < ${SCRIPT_DIR}/CHANNEL_NAME
read CREDENTIALS_ENDPOINT < ${SCRIPT_DIR}/CREDENTIALS_ENDPOINT
read POLICY_NAME < ${SCRIPT_DIR}/IOT_POLICY_NAME
read ROLE_ALIAS < ${SCRIPT_DIR}/ROLE_ALIAS
read THING_NAME < ${SCRIPT_DIR}/THING_NAME
read UNIQUE_ID < ${SCRIPT_DIR}/UNIQUE_ID

AWS_ROLE_TO_ASSUME="KVSWebRTCRole_${UNIQUE_ID}"
KVSWebRTCPolicy="KVSWebRTCPolicy_${UNIQUE_ID}"

echo "Cleaning up resources for thing: $THING_NAME"
# List certificates attached to thing
CERTIFICATES=$(aws iot list-thing-principals --thing-name "$THING_NAME" | jq -r '.principals[]' || echo "")
for CERT_ARN in $CERTIFICATES; do
  # Get certificate ID from ARN
  CERT_ID=$(echo $CERT_ARN | sed 's/.*cert\///')
  # Detach certificate from thing
  echo "Detaching certificate $CERT_ID from thing $THING_NAME"
  aws iot detach-thing-principal --thing-name "$THING_NAME" --principal "$CERT_ARN" || true
  # List and detach policies from certificate
  ATTACHED_POLICIES=$(aws iot list-attached-policies --target "$CERT_ARN" | jq -r '.policies[].policyName' || echo "")
  for POL in $ATTACHED_POLICIES; do
    echo "Detaching policy $POL from certificate $CERT_ID"
    aws iot detach-policy --policy-name "$POL" --target "$CERT_ARN" || true
  done
  # Deactivate and delete certificate
  echo "Deactivating certificate $CERT_ID"
  aws iot update-certificate --certificate-id "$CERT_ID" --new-status INACTIVE || true
  echo "Deleting certificate $CERT_ID"
  aws iot delete-certificate --certificate-id "$CERT_ID" --force || true
done
# Delete thing
echo "Deleting thing $THING_NAME"
aws iot delete-thing --thing-name "$THING_NAME" || true
# Delete role alias
echo "Deleting role alias $ROLE_ALIAS"
aws iot delete-role-alias --role-alias "$ROLE_ALIAS" || true
# Delete policy
echo "Deleting policy $POLICY_NAME"
aws iot delete-policy --policy-name "$POLICY_NAME" || true

echo "Deleting IAM role and policies..."
echo "First, list and delete all attached policies for role: $AWS_ROLE_TO_ASSUME"

# List all attached policies
ATTACHED_POLICIES=$(aws iam list-role-policies --role-name $AWS_ROLE_TO_ASSUME --query 'PolicyNames[]' --output text)

# Delete each attached policy
for POLICY in $ATTACHED_POLICIES
do
    echo "Deleting inline policy: $POLICY"
    aws iam delete-role-policy --role-name $AWS_ROLE_TO_ASSUME --policy-name $POLICY
done

# List and detach all managed policies
MANAGED_POLICIES=$(aws iam list-attached-role-policies --role-name $AWS_ROLE_TO_ASSUME --query 'AttachedPolicies[].PolicyArn' --output text)

for POLICY_ARN in $MANAGED_POLICIES
do
    echo "Detaching managed policy: $POLICY_ARN"
    aws iam detach-role-policy --role-name $AWS_ROLE_TO_ASSUME --policy-arn $POLICY_ARN
done

echo "Now deleting the role itself: $AWS_ROLE_TO_ASSUME"
aws iam delete-role --role-name $AWS_ROLE_TO_ASSUME

# Delete KVS signaling channel

# First, describe the channel to get the ARN
CHANNEL_ARN=$(aws kinesisvideo describe-signaling-channel --channel-name "$CHANNEL_NAME" --query 'ChannelInfo.ChannelARN' --output text 2>/dev/null || echo "")
if [ ! -z "$CHANNEL_ARN" ]; then
    echo "Deleting channel ARN: $CHANNEL_ARN"
    aws kinesisvideo delete-signaling-channel --channel-arn "$CHANNEL_ARN" || true
fi

echo "Deleting keys and certs"
rm -f ${SCRIPT_DIR}/../../private.key
rm -f ${SCRIPT_DIR}/../../public.key
rm -f ${SCRIPT_DIR}/../../certificate.pem

rm -f ${SCRIPT_DIR}/AWS_REGION
rm -f ${SCRIPT_DIR}/CHANNEL_NAME
rm -f ${SCRIPT_DIR}/UNIQUE_ID
rm -f ${SCRIPT_DIR}/THING_NAME
rm -f ${SCRIPT_DIR}/ROLE_ALIAS
rm -f ${SCRIPT_DIR}/CREDENTIALS_ENDPOINT
rm -f ${SCRIPT_DIR}/IOT_POLICY_NAME

echo "Clean up"
rm -f ${SCRIPT_DIR}/thing_output.json    
rm -f ${SCRIPT_DIR}/iot-policy.json
rm -f ${SCRIPT_DIR}/create-keys-and-certificate.json
rm -f ${SCRIPT_DIR}/trust-policy.json
rm -f ${SCRIPT_DIR}/role-policy.json
rm -f ${SCRIPT_DIR}/role-alias-policy.json